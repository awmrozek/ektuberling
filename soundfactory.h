/* -------------------------------------------------------------
   KDE Tuberling
   Sound factory
   mailto:ebischoff@nerim.net
 ------------------------------------------------------------- */


#ifndef _SOUNDFACTORY_H_
#define _SOUNDFACTORY_H_

#include "qobject.h"

class QDomDocument;
class TopLevel;

namespace Phonon
{
      class SimplePlayer;
}

class SoundFactory : public QObject
{
  Q_OBJECT

public:

  SoundFactory(TopLevel *parent, uint selectedLanguage);
  ~SoundFactory();

  void change(uint selectedLanguage);
  void playSound(const QString &soundRef) const;

protected:

  bool registerLanguages(QDomDocument &layoutDocument);
  bool loadLanguage(QDomDocument &layoutDocument, int toLoad);

private:

  void loadFailure();

private:

  int sounds;			// Number of sounds
  QString *namesList,		// List of sound names
  	  *filesList;           // List of sound files associated with each sound name

  TopLevel *topLevel;		// Top-level window
  Phonon::SimplePlayer *player;
};

#endif
