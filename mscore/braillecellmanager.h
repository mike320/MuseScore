#ifndef BRAILLECELLMANAGER_H
#define BRAILLECELLMANAGER_H

#include "libmscore/text.h"

namespace Ms {

//---------------------------------------------------------
//   MScoreTextToMBraille
//---------------------------------------------------------

class MScoreTextToBraille {

public:
      MScoreTextToBraille(const QString& tag, const QString& attr, const CharFormat& defFmt, const QString& mtf);
      static QString toPlainText(const QString& text);
      static QString toPlainTextPlusSymbols(const QList<TextFragment>& list);
      static bool split(const QList<TextFragment>& in, const int pos, const int len,
                        QList<TextFragment>& left, QList<TextFragment>& mid, QList<TextFragment>& right);
      void writeTextFragments(const QList<TextFragment>& fr, BrfWriter& brf);

private:
      QString updateFormat();
      QString attribs;
      QString tagname;
      CharFormat oldFormat;
      CharFormat newFormat;
      QString musicalTextFont;
};

} // namespace Ms

#endif // BRAILLECELLMANAGER_H
