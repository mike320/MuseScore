

//=============================================================================

#ifndef BRF_H
#define BRF_H

#include "connector.h"
#include "stafftype.h"
#include "interval.h"
#include "element.h"
#include "select.h"
//#include "xml.h"

namespace Ms {

//---------------------------------------------------------
//   BrfWriter
//---------------------------------------------------------

class BrfWriter : public QTextStream {
      static const int BS = 2048;

      Score* _score;
      QList<QString> stack;
      SelectionFilter _filter;

      Fraction _curTick    { 0, 1 };     // used to optimize output
      Fraction _tickDiff   { 0, 1 };
      int _curTrack        { -1 };
      int _trackDiff       { 0 };       // saved track is curTrack-trackDiff

      bool _clipboardmode  { false };   // used to modify write() behaviour
      bool _excerptmode    { false };   // true when writing a part
      bool _writeOmr       { true };    // false if writing into *.msc file
      bool _writeTrack     { false };
      bool _writePosition  { false };

      LinksIndexer _linksIndexer;
      QMap<int, int> _lidLocalIndices;

      std::vector<std::pair<const ScoreElement*, QString>> _elements;
      bool _recordElements = false;

      void putLevel();

   public:
      BrfWriter(Score*);
      BrfWriter(Score* s, QIODevice* dev);

      Fraction curTick() const            { return _curTick; }
      void setCurTick(const Fraction& v)  { _curTick   = v; }
      void incCurTick(const Fraction& v)  { _curTick += v; }

      int curTrack() const                { return _curTrack; }
      void setCurTrack(int v)             { _curTrack  = v; }

      Fraction tickDiff() const           { return _tickDiff; }
      void setTickDiff(const Fraction& v) { _tickDiff  = v; }

      int trackDiff() const               { return _trackDiff; }
      void setTrackDiff(int v)            { _trackDiff = v; }

      bool clipboardmode() const          { return _clipboardmode; }
      bool excerptmode() const            { return _excerptmode;   }
      bool writeOmr() const               { return _writeOmr;   }
      bool writeTrack() const             { return _writeTrack;    }
      bool writePosition() const          { return _writePosition; }

      void setClipboardmode(bool v)       { _clipboardmode = v; }
      void setExcerptmode(bool v)         { _excerptmode = v;   }
      void setWriteOmr(bool v)            { _writeOmr = v;      }
      void setWriteTrack(bool v)          { _writeTrack= v;     }
      void setWritePosition(bool v)       { _writePosition = v; }

      int assignLocalIndex(const Location& mainElementLocation);
      void setLidLocalIndex(int lid, int localIndex) { _lidLocalIndices.insert(lid, localIndex); }
      int lidLocalIndex(int lid) const { return _lidLocalIndices[lid]; }

      const std::vector<std::pair<const ScoreElement*, QString>>& elements() const { return _elements; }
      void setRecordElements(bool record) { _recordElements = record; }

      void sTag(const char* name, Spatium sp) { BrfWriter::tag(name, QVariant(sp.val())); }
      void pTag(const char* name, PlaceText);

      void header();

      void stag(const QString&);
      void etag();

      void stag(const ScoreElement* se, const QString& attributes = QString());
      void stag(const QString& name, const ScoreElement* se, const QString& attributes = QString());

      void tagE(const QString&);
      void tagE(const char* format, ...);
      void ntag(const char* name);
      void netag(const char* name);

      void tag(Pid id, void* data, void* defaultVal);
      void tag(Pid id, QVariant data, QVariant defaultData = QVariant());
      void tag(const char* name, QVariant data, QVariant defaultData = QVariant());
      void tag(const QString&, QVariant data);
      void tag(const char* name, const char* s)    { tag(name, QVariant(s)); }
      void tag(const char* name, const QString& s) { tag(name, QVariant(s)); }
      void tag(const char* name, const QWidget*);

      void comment(const QString&);

      void writeBrf(const QString&, QString s);
      void dump(int len, const unsigned char* p);

      void setFilter(SelectionFilter f) { _filter = f; }
      bool canWrite(const Element*) const;
      bool canWriteVoice(int track) const;

      static QString brfString(const QString&);
      static QString brfString(ushort c);
      };

}     // namespace Ms
#endif // BRF_H
