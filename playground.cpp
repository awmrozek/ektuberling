/*
    SPDX-FileCopyrightText: 1999-2006 Éric Bischoff <ebischoff@nerim.net>
    SPDX-FileCopyrightText: 2007-2008 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/* Play ground widget */

#include "playground.h"

#include <KConfig>
#include <KConfigGroup>
#include "ktuberling_debug.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDataStream>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsSvgItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPagedPaintDevice>

#include "action.h"
#include "filefactory.h"
#include "todraw.h"

static const char *saveGameTextScaleTextMode = "KTuberlingSaveGameV2";
static const char *saveGameTextTextMode = "KTuberlingSaveGameV3";
static const char *saveGameText = "KTuberlingSaveGameV4";

// Constructor
PlayGround::PlayGround(PlayGroundCallbacks *callbacks, QWidget *parent)
    : QGraphicsView(parent), m_callbacks(callbacks), m_newItem(nullptr), m_dragItem(nullptr), m_nextZValue(1), m_lockAspect(false), m_allowOnlyDrag(false)
{
  setFrameStyle(QFrame::NoFrame);
  setOptimizationFlag(QGraphicsView::DontSavePainterState, true); // all items here save the painter state
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setMouseTracking(true);
}

// Destructor
PlayGround::~PlayGround()
{
  for (const SceneData &data : std::as_const(m_scenes))
  {
    delete data.scene;
    delete data.undoStack;
  }
}

// Reset the play ground
void PlayGround::reset()
{

  const QList<QGraphicsItem*> items = scene()->items();
  for(QGraphicsItem *item : items)
  {
    ToDraw *currentObject = qgraphicsitem_cast<ToDraw *>(item);
    delete currentObject;
  }

  undoStack()->clear();
}

// Save objects laid down on the editable area
bool PlayGround::saveAs(const QString & name)
{
  QFile f(name);
  if (!f.open( QIODevice::WriteOnly ) )
      return false;

  QFileInfo gameBoard(m_gameboardFile);
  QDataStream out(&f);
  out.setVersion(QDataStream::Qt_4_5);
  out << QString::fromLatin1(saveGameText);
  out << gameBoard.fileName();
  const QList<QGraphicsItem*> items = scene()->items();
  for(QGraphicsItem *item : items)
  {
    ToDraw *currentObject = qgraphicsitem_cast<ToDraw *>(item);
    if (currentObject != nullptr) currentObject->save(out);
  }

  return (f.error() == QFile::NoError);
}

// Print gameboard's picture
bool PlayGround::printPicture(QPagedPaintDevice &printer)
{
  QPainter artist;
  QPixmap picture(getPicture());

  if (!artist.begin(&printer)) return false;
  artist.drawPixmap(QPoint(32, 32), picture);
  if (!artist.end()) return false;
  return true;
}

// Get a pixmap containing the current picture
QPixmap PlayGround::getPicture()
{
  QPixmap result(mapFromScene(backgroundRect()).boundingRect().size());
  QPainter artist(&result);
  scene()->render(&artist, QRectF(), backgroundRect(), Qt::IgnoreAspectRatio);
  artist.end();
  return result;
}

void PlayGround::connectRedoAction(QAction *action)
{
  connect(action, &QAction::triggered, &m_undoGroup, &QUndoGroup::redo);
  connect(&m_undoGroup, &QUndoGroup::canRedoChanged, action, &QAction::setEnabled);
}

void PlayGround::connectUndoAction(QAction *action)
{
  connect(action, &QAction::triggered, &m_undoGroup, &QUndoGroup::undo);
  connect(&m_undoGroup, &QUndoGroup::canUndoChanged, action, &QAction::setEnabled);
}

// Mouse pressed event
void PlayGround::mousePressEvent(QMouseEvent *event)
{
  if (m_gameboardFile.isEmpty()) return;

  if (event->button() != Qt::LeftButton) return;

  m_mousePressPos = event->pos();

  if (m_dragItem) placeDraggedItem(event->pos());
  else if (m_newItem) placeNewItem(event->pos());
  else
  {
    // see if the user clicked on the warehouse of items
    QPointF scenePos = mapToScene(event->pos());
    QMap<QString, QString>::const_iterator it, itEnd;
    it = m_objectsNameSound.constBegin();
    itEnd = m_objectsNameSound.constEnd();
    QString foundElem;
    QRectF bounds;
    for( ; foundElem.isNull() && it != itEnd; ++it)
    {
      bounds = m_SvgRenderer.boundsOnElement(it.key());
      if (bounds.contains(scenePos)) foundElem = it.key();
    }

    if (!foundElem.isNull())
    {
      const double objectScale = m_objectsNameRatio.value(foundElem);
      const QSizeF elementSize = m_SvgRenderer.boundsOnElement(foundElem).size() * objectScale;
      QPointF itemPos = mapToScene(event->pos());
      itemPos -= QPointF(elementSize.width()/2, elementSize.height()/2);

      m_callbacks->playSound(m_objectsNameSound.value(foundElem));

      m_newItem = new ToDraw;
      m_newItem->setBeingDragged(true);
      m_newItem->setPos(clipPos(itemPos, m_newItem));
      m_newItem->setSharedRenderer(&m_SvgRenderer);
      m_newItem->setElementId(foundElem);
      m_newItem->setZValue(m_nextZValue);
      m_nextZValue++;
      m_newItem->setTransform(QTransform::fromScale(objectScale, objectScale));

      scene()->addItem(m_newItem);
      setCursor(Qt::BlankCursor);
    }
    else
    {
      // see if the user clicked on an already existent item
      QGraphicsItem *dragItem = scene()->itemAt(mapToScene(event->pos()), QTransform());
      m_dragItem = qgraphicsitem_cast<ToDraw*>(dragItem);
      if (m_dragItem)
      {
        QString elem = m_dragItem->elementId();

        m_callbacks->playSound(m_objectsNameSound.value(elem));
        setCursor(Qt::BlankCursor);
        m_dragItem->setBeingDragged(true);
        m_itemDraggedPos = m_dragItem->pos();

        const QSizeF elementSize = m_dragItem->transform().mapRect(m_dragItem->unclippedRect()).size();
        QPointF itemPos = mapToScene(event->pos());
        itemPos -= QPointF(elementSize.width()/2, elementSize.height()/2);
        m_dragItem->setPos(clipPos(itemPos, m_dragItem));
      }
    }
  }
}

void PlayGround::mouseMoveEvent(QMouseEvent *event)
{
  ToDraw *movingItem = m_newItem ? m_newItem : m_dragItem;
  if (movingItem) {
    QPointF itemPos = mapToScene(event->pos());
    const QSizeF elementSize = movingItem->transform().mapRect(movingItem->unclippedRect()).size();
    itemPos -= QPointF(elementSize.width()/2, elementSize.height()/2);

    movingItem->setPos(clipPos(itemPos, movingItem));
  }
}

void PlayGround::mouseReleaseEvent(QMouseEvent *event)
{
  QPoint point = event->pos() - m_mousePressPos;
  if (m_allowOnlyDrag || point.manhattanLength() > qApp->startDragDistance()) {
      if (m_dragItem) placeDraggedItem(event->pos());
      else if (m_newItem) placeNewItem(event->pos());
  }
}

bool PlayGround::insideBackground(const QSizeF &size, const QPointF &pos) const
{
  return backgroundRect().intersects(QRectF(pos, size));
}

QPointF PlayGround::clipPos(const QPointF &p, ToDraw *item) const
{
  const qreal objectScale = m_objectsNameRatio.value(item->elementId());

  QPointF res = p;
  res.setX(qMax(qreal(0), res.x()));
  res.setY(qMax(qreal(0), res.y()));
  res.setX(qMin(m_SvgRenderer.defaultSize().width() - item->boundingRect().width() * objectScale, res.x()));
  res.setY(qMin(m_SvgRenderer.defaultSize().height()- item->boundingRect().height() * objectScale, res.y()));
  return res;
}

QRectF PlayGround::backgroundRect() const
{
  return m_SvgRenderer.boundsOnElement(QStringLiteral( "background" ));
}

void PlayGround::placeDraggedItem(const QPoint &pos)
{
  QPointF itemPos = mapToScene(pos);
  const QSizeF &elementSize = m_dragItem->transform().mapRect(m_dragItem->unclippedRect()).size();
  itemPos -= QPointF(elementSize.width()/2, elementSize.height()/2);

  if (insideBackground(elementSize, itemPos))
  {
    m_dragItem->setBeingDragged(false);
    undoStack()->push(new ActionMove(m_dragItem, m_itemDraggedPos, m_nextZValue, scene()));
    m_nextZValue++;
  }
  else
  {
    undoStack()->push(new ActionRemove(m_dragItem, m_itemDraggedPos, scene()));
  }

  setCursor(QCursor());
  m_dragItem = nullptr;
}

void PlayGround::placeNewItem(const QPoint &pos)
{
  const QSizeF elementSize = m_newItem->transform().mapRect(m_newItem->unclippedRect()).size();
  QPointF itemPos = mapToScene(pos);
  itemPos -= QPointF(elementSize.width()/2, elementSize.height()/2);
  if (insideBackground(elementSize, itemPos))
  {
    m_newItem->setBeingDragged(false);
    undoStack()->push(new ActionAdd(m_newItem, scene()));
  } else {
    m_newItem->deleteLater();
  }
  m_newItem = nullptr;
  setCursor(QCursor());
}

void PlayGround::recenterView()
{
  // Cannot use sceneRect() because sometimes items get placed
  // with pos() outside rect (e.g. pizza theme)
  fitInView(QRect(QPoint(0,0), m_SvgRenderer.defaultSize()),
      m_lockAspect ? Qt::KeepAspectRatio : Qt::IgnoreAspectRatio);
}

QGraphicsScene *PlayGround::scene() const
{
  return m_scenes[m_gameboardFile].scene;
}

QUndoStack *PlayGround::undoStack() const
{
  return m_scenes[m_gameboardFile].undoStack;
}

void PlayGround::resizeEvent(QResizeEvent *)
{
  recenterView();
}

void PlayGround::lockAspectRatio(bool lock)
{
  if (m_lockAspect != lock)
  {
    m_lockAspect = lock;
    recenterView();
  }
}

bool PlayGround::isAspectRatioLocked() const
{
  return m_lockAspect;
}

// Register the various playgrounds
void PlayGround::registerPlayGrounds()
{
  QSet<QString> list;
  const QStringList dirs = FileFactory::locateAll(QStringLiteral("pics"));
  for (const QString &dir : dirs)
  {
    const QStringList fileNames = QDir(dir).entryList(QStringList() << QStringLiteral("*.theme"));
    for (const QString &file : fileNames)
    {
        list << dir + '/' + file;
    }
  }

  QMultiMap<QString, QPair<QString, QPixmap>> sortedByName;

  for(const QString &theme : std::as_const(list))
  {
    QFile layoutFile(theme);
    if (layoutFile.open(QIODevice::ReadOnly))
    {
      QDomDocument layoutDocument;
      if (layoutDocument.setContent(&layoutFile))
      {
        QString desktop = layoutDocument.documentElement().attribute(QStringLiteral( "desktop" ));
        KConfig c( FileFactory::locate( QLatin1String( "pics/" ) + desktop ) );
        KConfigGroup cg = c.group("KTuberlingTheme");
        QString gameboard = layoutDocument.documentElement().attribute(QStringLiteral( "gameboard" ));
        QPixmap pixmap(200,100);
        pixmap.fill(Qt::transparent);
        playGroundPixmap(gameboard,pixmap);
        sortedByName.insert(cg.readEntry("Name"), QPair<QString, QPixmap>(theme, pixmap));
      }
    }
  }

  for(auto it = sortedByName.begin(); it != sortedByName.end(); ++it) {
    m_callbacks->registerGameboard(it.key(), it.value().first, it.value().second);
  }

}

void PlayGround::playGroundPixmap(const QString &playgroundName, QPixmap &pixmap)
{
  m_SvgRenderer.load(FileFactory::locate(QLatin1String( "pics/" ) + playgroundName ));
  QPainter painter(&pixmap);
  m_SvgRenderer.render(&painter,QStringLiteral( "background" ));
}

// Load background and draggable objects masks
bool PlayGround::loadPlayGround(const QString &gameboardFile)
{
  QFile layoutFile(gameboardFile);
  if (!layoutFile.open(QIODevice::ReadOnly)) return false;
  QDomDocument layoutDocument;
  if (!layoutDocument.setContent(&layoutFile)) return false;

  const QDomElement playGroundElement = layoutDocument.documentElement();

  QString gameboardName = playGroundElement.attribute(QStringLiteral( "gameboard" ));

  QColor bgColor = QColor(playGroundElement.attribute(QStringLiteral( "bgcolor" ), QStringLiteral( "#fff" ) ) );
  if (!bgColor.isValid())
    bgColor = Qt::white;

  if (!m_SvgRenderer.load(FileFactory::locate( QLatin1String( "pics/" ) + gameboardName )))
    return false;

  const QDomNodeList objectsList = playGroundElement.elementsByTagName(QStringLiteral( "object" ));
  if (objectsList.count() < 1)
    return false;

  m_objectsNameSound.clear();

  // create scene data if needed
  if(!m_scenes.contains(gameboardFile))
  {
    SceneData &data = m_scenes[gameboardFile];
    data.scene = new QGraphicsScene();
    data.undoStack = new QUndoStack();

    QGraphicsSvgItem *background = new QGraphicsSvgItem();
    background->setPos(QPoint(0,0));
    background->setSharedRenderer(&m_SvgRenderer);
    background->setZValue(0);
    data.scene->addItem(background);

    m_undoGroup.addStack(data.undoStack);
  }

  for (int decoration = 0; decoration < objectsList.count(); decoration++)
  {
    const QDomElement objectElement = objectsList.item(decoration).toElement();

    const QString &objectName = objectElement.attribute(QStringLiteral( "name" ));
    if (m_SvgRenderer.elementExists(objectName))
    {
      m_objectsNameSound.insert(objectName, objectElement.attribute(QStringLiteral( "sound" )));
      m_objectsNameRatio.insert(objectName, objectElement.attribute(QStringLiteral( "scale" ), QStringLiteral( "1" )).toDouble());
    }
    else
    {
      qCWarning(KTUBERLING_LOG) << objectName << "does not exist. Check" << gameboardFile;
    }
  }

  setBackgroundBrush(bgColor);
  m_gameboardFile = gameboardFile;
  setScene(scene());

  recenterView();

  m_undoGroup.setActiveStack(undoStack());

  return true;
}

void PlayGround::setAllowOnlyDrag(bool allowOnlyDrag)
{
  m_allowOnlyDrag = allowOnlyDrag;
}

QString PlayGround::currentGameboard() const
{
  return m_gameboardFile;
}

// Load objects and lay them down on the editable area
PlayGround::LoadError PlayGround::loadFrom(const QString &name)
{
  QFile f(name);
  if (!f.open(QIODevice::ReadOnly))
      return OtherError;

  QDataStream in(&f);
  in.setVersion(QDataStream::Qt_4_5);

  bool scale = false;
  bool reopenInTextMode = false;
  QString magicText;
  in >> magicText;
  if ( QLatin1String( saveGameTextScaleTextMode ) == magicText) {
      scale = true;
      reopenInTextMode = true;
  } else if (QLatin1String( saveGameTextTextMode ) == magicText) {
      reopenInTextMode = true;
  } else if ( QLatin1String( saveGameText ) != magicText) {
      return OldFileVersionError;
  }

  if (reopenInTextMode) {
      f.close();
      if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return OtherError;
      in.setDevice(&f);
      in.setVersion(QDataStream::Qt_4_5);
      in >> magicText;
  }

  sceneRect();

  if (in.atEnd())
    return OtherError;

  QString board;
  in >> board;

  qreal xFactor = 1.0;
  qreal yFactor = 1.0;
  m_callbacks->changeGameboard(board);

  reset();

  if (scale) {
    QSize defaultSize = m_SvgRenderer.defaultSize();
    QSize currentSize = size();
    xFactor = (qreal)defaultSize.width() / (qreal)currentSize.width();
    yFactor = (qreal)defaultSize.height() / (qreal)currentSize.height();
  }

  while ( !in.atEnd() )
  {
    ToDraw *obj = new ToDraw;
    if (!obj->load(in))
    {
      delete obj;
      return OtherError;
    }
    obj->setSharedRenderer(&m_SvgRenderer);
    double objectScale = m_objectsNameRatio.value(obj->elementId());
    obj->setTransform(QTransform::fromScale(objectScale, objectScale));
    if (scale) { // Mimic old behavior
      QPointF storedPos = obj->pos();
      storedPos.setX(storedPos.x() * xFactor);
      storedPos.setY(storedPos.y() * yFactor);
      obj->setPos(storedPos);
    }
    scene()->addItem(obj);
    undoStack()->push(new ActionAdd(obj, scene()));
  }
  if (f.error() == QFile::NoError) return NoError;
  else return OtherError;
}

/* kate: replace-tabs on; indent-width 2; */
