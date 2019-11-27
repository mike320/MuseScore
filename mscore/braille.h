#ifndef BRAILLE_H
#define BRAILLE_H

#include "libmscore/fraction.h"
#include "libmscore/mscore.h"
#include "libmscore/pitchspelling.h"
#include "libmscore/line.h"
#include "importxmlfirstpass.h"
#include "musicxmlsupport.h"

namespace Ms {

//---------------------------------------------------------
//   MusicXmlPartGroup
//---------------------------------------------------------

struct BraillePartGroup {
      int span;
      int start;
      BracketType type;
      bool barlineSpan;
      };

const int MAX_LYRICS       = 16;
const int MAX_PART_GROUPS  = 8;
const int MAX_NUMBER_LEVEL = 6; // maximum number of overlapping MusicXML objects
const int MAX_BRACKETS     = 9; // 8;
const int MAX_DASHES       = 8;

//---------------------------------------------------------
//   CreditWords
//    a single parsed MusicXML credit-words element
//---------------------------------------------------------

struct CreditWords {
      double defaultX;
      double defaultY;
      QString justify;
      QString hAlign;
      QString vAlign;
      QString words;
      CreditWords(double a, double b, QString c, QString d, QString e, QString f)
            {
            defaultX = a;
            defaultY = b;
            justify  = c;
            hAlign   = d;
            vAlign   = e;
            words    = f;
            }
      };

typedef  QList<CreditWords*> CreditWordsList;
typedef  CreditWordsList::iterator iCreditWords;
typedef  CreditWordsList::const_iterator ciCreditWords;

//---------------------------------------------------------
//   JumpMarkerDesc
//---------------------------------------------------------

/**
 The description of Jumps and Markers to be added later
*/

class JumpMarkerDesc {
      Element* _el;
      Measure* _meas;

public:
      JumpMarkerDesc(Element* el, Measure* meas) : _el(el), _meas(meas) {}
      Element* el() const { return _el; }
      Measure* meas() const { return _meas; }
      };

typedef QList<JumpMarkerDesc> JumpMarkerDescList;

//---------------------------------------------------------
//   SlurDesc
//---------------------------------------------------------

/**
 The description of Slurs being handled
 */

class SlurDesc {
public:
      enum class State : char { NONE, START, STOP };
      SlurDesc() : _slur(0), _state(State::NONE) {}
      Slur* slur() const { return _slur; }
      void start(Slur* slur) { _slur = slur; _state = State::START; }
      void stop(Slur* slur) { _slur = slur; _state = State::STOP; }
      bool isStart() const { return _state == State::START; }
      bool isStop() const { return _state == State::STOP; }
private:
      Slur* _slur;
      State _state;
      };

//---------------------------------------------------------
//   MusicXml
//---------------------------------------------------------

typedef std::vector<MusicXmlPartGroup*> MusicXmlPartGroupList;
typedef QMap<SLine*, QPair<int, int> > MusicXmlSpannerMap;

enum {
      NoSystem          = 0,
      TopSystem         = 1,
      NewSystem         = 2,
      NewPage           = 3
      };

#endif // BRAILLE_H
