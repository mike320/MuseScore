//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2019 MuseScore BVA
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================


#include "musescore.h"
#include <math.h>
#include "config.h"
#include "file.h"
#include "libmscore/score.h"
#include "libmscore/rest.h"
#include "libmscore/chord.h"
#include "libmscore/sig.h"
#include "libmscore/key.h"
#include "libmscore/clef.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/brf.h"
#include "libmscore/beam.h"
#include "libmscore/staff.h"
#include "libmscore/part.h"
#include "libmscore/measure.h"
#include "libmscore/style.h"
// #include "musicxml.h"       will delete this line when file replaced
#include "libmscore/slur.h"
#include "libmscore/hairpin.h"
#include "libmscore/dynamic.h"
#include "libmscore/barline.h"
#include "libmscore/timesig.h"
#include "libmscore/ottava.h"
#include "libmscore/pedal.h"
#include "libmscore/text.h"
#include "libmscore/tuplet.h"
#include "libmscore/lyrics.h"
#include "libmscore/volta.h"
#include "libmscore/keysig.h"
#include "libmscore/bracket.h"
#include "libmscore/arpeggio.h"
#include "libmscore/jump.h"
#include "libmscore/marker.h"
#include "libmscore/tremolo.h"
#include "libmscore/trill.h"
#include "libmscore/harmony.h"
#include "libmscore/tempotext.h"
#include "libmscore/sym.h"
#include "libmscore/pitchspelling.h"
#include "libmscore/utils.h"
#include "libmscore/articulation.h"
#include "libmscore/page.h"
#include "libmscore/system.h"
#include "libmscore/element.h"
#include "libmscore/glissando.h"
#include "libmscore/navigate.h"
#include "libmscore/spanner.h"
#include "libmscore/drumset.h"
#include "preferences.h"
#include "libmscore/mscore.h"
#include "libmscore/accidental.h"
#include "libmscore/breath.h"
#include "libmscore/chordline.h"
#include "libmscore/figuredbass.h"
#include "libmscore/stringdata.h"
#include "libmscore/rehearsalmark.h"
#include "thirdparty/qzip/qzipwriter_p.h"
#include "libmscore/fret.h"
#include "libmscore/tie.h"
#include "libmscore/undo.h"
#include "libmscore/textline.h"
#include "libmscore/fermata.h"
#include "braillecellmanager.h"

namespace Ms {

//---------------------------------------------------------
//   local defines for debug output
//---------------------------------------------------------

// #define DEBUG_CLEF true
// #define DEBUG_REPEATS true
// #define DEBUG_TICK true

#ifdef DEBUG_CLEF
#define clefDebug(...) qDebug(__VA_ARGS__)
#else
#define clefDebug(...) {}
#endif

//---------------------------------------------------------
//   typedefs
//---------------------------------------------------------

typedef QMap<int, const FiguredBass*> FigBassMap;

//---------------------------------------------------------
//   attributes -- prints <attributes> tag when necessary
//---------------------------------------------------------

class Attributes {
      bool inAttributes;

public:
      Attributes() { start(); }
      void doAttr(BrfWriter& braille, bool attr);
      void start();
      void stop(BrfWriter& braille);
      };

//---------------------------------------------------------
//   doAttr - when necessary change state and print <attributes> tag
//---------------------------------------------------------

void Attributes::doAttr(BrfWriter& braille, bool attr)
      {
      if (!inAttributes && attr) {
            braille.stag("attributes");
            inAttributes = true;
            }
      else if (inAttributes && !attr) {
            braille.etag();
            inAttributes = false;
            }
      }

//---------------------------------------------------------
//   start -- initialize
//---------------------------------------------------------

void Attributes::start()
      {
      inAttributes = false;
      }

//---------------------------------------------------------
//   stop -- print </attributes> tag when necessary
//---------------------------------------------------------

void Attributes::stop(BrfWriter& braille)
      {
      if (inAttributes) {
            braille.etag();
            inAttributes = false;
            }
      }

//---------------------------------------------------------
//   notations -- prints <notations> tag when necessary
//---------------------------------------------------------

class Notations {
      bool notationsPrinted;

public:
      Notations() { notationsPrinted = false; }
      void tag(BrfWriter& brf);
      void etag(BrfWriter& brf);
      };

//---------------------------------------------------------
//   articulations -- prints <articulations> tag when necessary
//---------------------------------------------------------

class Articulations {
      bool articulationsPrinted;

public:
      Articulations() { articulationsPrinted = false; }
      void tag(BrfWriter& brf);
      void etag(BrfWriter& braille);
      };

//---------------------------------------------------------
//   ornaments -- prints <ornaments> tag when necessary
//---------------------------------------------------------

class Ornaments {
      bool ornamentsPrinted;

public:
      Ornaments() { ornamentsPrinted = false; }
      void tag(BrfWriter& brf);
      void etag(BrfWriter& brf);
      };

//---------------------------------------------------------
//   technical -- prints <technical> tag when necessary
//---------------------------------------------------------

class Technical {
      bool technicalPrinted;

public:
      Technical() { technicalPrinted = false; }
      void tag(BrfWriter& brf);
      void etag(BrfWriter& brf);
      };

//---------------------------------------------------------
//   slur handler -- prints <slur> tags
//---------------------------------------------------------

class SlurHandler {
      const Slur* slur[MAX_NUMBER_LEVEL];
      bool started[MAX_NUMBER_LEVEL];
      int findSlur(const Slur* s) const;

public:
      SlurHandler();
      void doSlurs(const ChordRest* chordRest, Notations& notations, BrfWriter& brf);

private:
      void doSlurStart(const Slur* s, Notations& notations, BrfWriter& brf);
      void doSlurStop(const Slur* s, Notations& notations, BrfWriter& braille);
      };

//---------------------------------------------------------
//   glissando handler -- prints <glissando> tags
//---------------------------------------------------------

class GlissandoHandler {
      const Note* glissNote[MAX_NUMBER_LEVEL];
      const Note* slideNote[MAX_NUMBER_LEVEL];
      int findNote(const Note* note, int type) const;

public:
      GlissandoHandler();
      void doGlissandoStart(Glissando* gliss, Notations& notations, BrfWriter& brf);
      void doGlissandoStop(Glissando* gliss, Notations& notations, BrfWriter& brf);
      };

//---------------------------------------------------------
//   ExportBraille
//---------------------------------------------------------

typedef QHash<const Chord*, const Trill*> TrillHash;
typedef QMap<const Instrument*, int> MbrfInstrumentMap;

class ExportBrf {
      Score* _score;
      BrfWriter _brf;
      SlurHandler sh;
      GlissandoHandler gh;
      Fraction _tick;
      Attributes _attr;
      TextLine const* brackets[MAX_NUMBER_LEVEL];
      TextLineBase const* dashes[MAX_NUMBER_LEVEL];
      Hairpin const* hairpins[MAX_NUMBER_LEVEL];
      Ottava const* ottavas[MAX_NUMBER_LEVEL];
      Trill const* trills[MAX_NUMBER_LEVEL];
      int div;
      double millimeters;
      int tenths;
      TrillHash _trillStart;
      TrillHash _trillStop;
      MbrfInstrumentMap instrMap;

      int findBracket(const TextLine* tl) const;
      int findDashes(const TextLineBase* tl) const;
      int findHairpin(const Hairpin* tl) const;
      int findOttava(const Ottava* tl) const;
      int findTrill(const Trill* tl) const;
      void chord(Chord* chord, int staff, const std::vector<Lyrics*>* ll, bool useDrumset);
      void rest(Rest* chord, int staff);
      void clef(int staff, const ClefType ct, const QString& extraAttributes = "");
      void timesig(TimeSig* tsig);
      void keysig(const KeySig* ks, ClefType ct, int staff = 0, bool visible = true);
      void barlineLeft(Measure* m);
      void barlineMiddle(const BarLine* bl);
      void barlineRight(Measure* m);
      void lyrics(const std::vector<Lyrics*>* ll, const int trk);
      void work(const MeasureBase* measure);
      void calcDivMoveToTick(const Fraction& t);
      void calcDivisions();
      void keysigTimesig(const Measure* m, const Part* p);
      void chordAttributes(Chord* chord, Notations& notations, Technical& technical,
                           TrillHash& trillStart, TrillHash& trillStop);
      void wavyLineStartStop(Chord* chord, Notations& notations, Ornaments& ornaments,
                             TrillHash& trillStart, TrillHash& trillStop);
      void print(const Measure* const m, const int partNr, const int firstStaffOfPart, const int nrStavesInPart);
      void findAndExportClef(Measure* m, const int staves, const int strack, const int etrack);
      void exportDefaultClef(const Part* const part, const Measure* const m);
      void writeElement(Element* el, const Measure* m, int sstaff, bool useDrumset);

public:
      ExportBrf(Score* s)
            : _brf(s)
            {
            _score = s; _tick = { 0,1 }; div = 1; tenths = 40;
            millimeters = _score->spatium() * tenths / (10 * DPMM);
            }
      void write(QIODevice* dev);
      void credits(BrfWriter& brf);
      void moveToTick(const Fraction& t);
      void words(TextBase const* const text, int staff);
      void rehearsal(RehearsalMark const* const rmk, int staff);
      void hairpin(Hairpin const* const hp, int staff, const Fraction& tick);
      void ottava(Ottava const* const ot, int staff, const Fraction& tick);
      void pedal(Pedal const* const pd, int staff, const Fraction& tick);
      void textLine(TextLine const* const tl, int staff, const Fraction& tick);
      void dynamic(Dynamic const* const dyn, int staff);
      void symbol(Symbol const* const sym, int staff);
      void tempoText(TempoText const* const text, int staff);
      void harmony(Harmony const* const, FretDiagram const* const fd, int offset = 0);
      Score* score() const { return _score; };
      double getTenthsFromInches(double) const;
      double getTenthsFromDots(double) const;
      };

//---------------------------------------------------------
//   addPositioningAttributes
//   According to the specs (common.dtd), all direction-type and note elements must be relative to the measure
//   while all other elements are relative to their position or the nearest note.
//---------------------------------------------------------

static QString addPositioningAttributes(Element const* const el, bool isSpanStart = true)
      {
      if (!preferences.getBool(PREF_EXPORT_BRAILLE_EXPORTLAYOUT))
            return "";

      //qDebug("single el %p _pos x,y %f %f _userOff x,y %f %f spatium %f",
      //       el, el->ipos().x(), el->ipos().y(), el->offset().x(), el->offset().y(), el->spatium());

      const float positionElipson = 0.1f;
      float defaultX = 0;
      float defaultY = 0;
      float relativeX = 0;
      float relativeY = 0;
      float spatium = el->spatium();

      const SLine* span = nullptr;
      if (el->isSLine())
            span = static_cast<const SLine*>(el);

      if (span && !span->segmentsEmpty()) {
            if (isSpanStart) {
                  const auto seg = span->frontSegment();
                  const auto offset = seg->offset();
                  const auto p = seg->pos();
                  relativeX = offset.x();
                  defaultY = p.y();

                  //qDebug("sline start seg %p seg->pos x,y %f %f seg->userOff x,y %f %f spatium %f",
                  //       seg, p.x(), p.y(), seg->offset().x(), seg->offset().y(), seg->spatium());

                  }
            else {
                  const auto seg = span->backSegment();
                  const auto userOff = seg->offset(); // This is the offset accessible from the inspector
                  const auto userOff2 = seg->userOff2(); // Offset of the actual dragged anchor, which doesn't affect the inspector offset
                  //auto pos = seg->pos();
                  //auto pos2 = seg->pos2();

                  //qDebug("sline stop seg %p seg->pos2 x,y %f %f seg->userOff2 x,y %f %f spatium %f",
                  //       seg, pos2.x(), pos2.y(), seg->userOff2().x(), seg->userOff2().y(), seg->spatium());

                  // For an SLine, the actual offset equals the sum of userOff and userOff2,
                  // as userOff moves the SLine as a whole
                  relativeX = userOff.x() + userOff2.x();

                  // Following would probably required for non-horizontal SLines:
                  //defaultY = pos.y() + pos2.y();
                  }
            }
      else {
            defaultX = el->ipos().x();   // Note: for some elements, Finale Notepad seems to work slightly better w/o default-x
            defaultY = el->ipos().y();
            relativeX = el->offset().x();
            relativeY = el->offset().y();
            }

      // convert into spatium tenths for MusicXML
      defaultX *=  10 / spatium;
      defaultY *=  -10 / spatium;
      relativeX *=  10 / spatium;
      relativeY *=  -10 / spatium;

      QString res;
      if (fabsf(defaultX) > positionElipson)
            res += QString(" default-x=\"%1\"").arg(QString::number(defaultX, 'f', 2));
      if (fabsf(defaultY) > positionElipson)
            res += QString(" default-y=\"%1\"").arg(QString::number(defaultY, 'f', 2));
      if (fabsf(relativeX) > positionElipson)
            res += QString(" relative-x=\"%1\"").arg(QString::number(relativeX, 'f', 2));
      if (fabsf(relativeY) > positionElipson)
            res += QString(" relative-y=\"%1\"").arg(QString::number(relativeY, 'f', 2));

      return res;
      }

//---------------------------------------------------------
//   tag
//---------------------------------------------------------

void Notations::tag(BrfWriter& brf)
      {
      if (!notationsPrinted)
            brf.stag("notations");
      notationsPrinted = true;
      }

//---------------------------------------------------------
//   etag
//---------------------------------------------------------

void Notations::etag(BrfWriter& brf)
      {
      if (notationsPrinted)
            brf.etag();
      notationsPrinted = false;
      }

//---------------------------------------------------------
//   tag
//---------------------------------------------------------

void Articulations::tag(BrfWriter& brf)
      {
      if (!articulationsPrinted)
            brf.stag("articulations");
      articulationsPrinted = true;
      }

//---------------------------------------------------------
//   etag
//---------------------------------------------------------

void Articulations::etag(BrfWriter& brf)
      {
      if (articulationsPrinted)
            brf.etag();
      articulationsPrinted = false;
      }

//---------------------------------------------------------
//   tag
//---------------------------------------------------------

void Ornaments::tag(BrfWriter& brf)
      {
      if (!ornamentsPrinted)
            brf.stag("ornaments");
      ornamentsPrinted = true;
      }

//---------------------------------------------------------
//   etag
//---------------------------------------------------------

void Ornaments::etag(BrfWriter& brf)
      {
      if (ornamentsPrinted)
            brf.etag();
      ornamentsPrinted = false;
      }

//---------------------------------------------------------
//   tag
//---------------------------------------------------------

void Technical::tag(BrfWriter& brf)
      {
      if (!technicalPrinted)
            brf.stag("technical");
      technicalPrinted = true;
      }

//---------------------------------------------------------
//   etag
//---------------------------------------------------------

void Technical::etag(BrfWriter& brf)
      {
      if (technicalPrinted)
            brf.etag();
      technicalPrinted = false;
      }

//---------------------------------------------------------
//   color2xml
//---------------------------------------------------------

/**
 Return \a el color.
 */

static QString color2xml(const Element* el)
      {
      if (el->color() != MScore::defaultColor)
            return QString(" color=\"%1\"").arg(el->color().name().toUpper());
      else
            return "";
      }

//---------------------------------------------------------
//   slurHandler
//---------------------------------------------------------

SlurHandler::SlurHandler()
      {
      for (int i = 0; i < MAX_NUMBER_LEVEL; ++i) {
            slur[i] = 0;
            started[i] = false;
            }
      }

static QString slurTieLineStyle(const SlurTie* s)
      {
      QString lineType;
      QString rest;
      switch (s->lineType()) {
            case 1:
                  lineType = "dotted";
                  break;
            case 2:
                  lineType = "dashed";
                  break;
            default:
                  lineType = "";
            }
      if (!lineType.isEmpty())
            rest = QString(" line-type=\"%1\"").arg(lineType);
      return rest;
      }

//---------------------------------------------------------
//   findSlur -- get index of slur in slur table
//   return -1 if not found
//---------------------------------------------------------

int SlurHandler::findSlur(const Slur* s) const
      {
      for (int i = 0; i < MAX_NUMBER_LEVEL; ++i)
            if (slur[i] == s) return i;
      return -1;
      }

//---------------------------------------------------------
//   findFirstChordRest -- find first chord or rest (in musical order) for slur s
//   note that this is not necessarily the same as s->startElement()
//---------------------------------------------------------

static const ChordRest* findFirstChordRest(const Slur* s)
      {
      const Element* e1 = s->startElement();
      if (!e1 || !(e1->isChordRest())) {
            qDebug("no valid start element for slur %p", s);
            return nullptr;
            }

      const Element* e2 = s->endElement();
      if (!e2 || !(e2->isChordRest())) {
            qDebug("no valid end element for slur %p", s);
            return nullptr;
            }

      if (e1->tick() < e2->tick())
            return static_cast<const ChordRest*>(e1);
      else if (e1->tick() > e2->tick())
            return static_cast<const ChordRest*>(e2);

      if (e1->isRest() || e2->isRest()) {
            return nullptr;
            }

      const auto c1 = static_cast<const Chord*>(e1);
      const auto c2 = static_cast<const Chord*>(e2);

      // c1->tick() == c2->tick()
      if (!c1->isGrace() && !c2->isGrace()) {
            // slur between two regular notes at the same tick
            // probably shouldn't happen but handle just in case
            qDebug("invalid slur between chords %p and %p at tick %d", c1, c2, c1->tick().ticks());
            return 0;
            }
      else if (c1->isGraceBefore() && !c2->isGraceBefore())
            return c1;        // easy case: c1 first
      else if (c1->isGraceAfter() && !c2->isGraceAfter())
            return c2;        // easy case: c2 first
      else if (c2->isGraceBefore() && !c1->isGraceBefore())
            return c2;        // easy case: c2 first
      else if (c2->isGraceAfter() && !c1->isGraceAfter())
            return c1;        // easy case: c1 first
      else {
            // both are grace before or both are grace after -> compare grace indexes
            // (note: higher means closer to the non-grace chord it is attached to)
            if ((c1->isGraceBefore() && c1->graceIndex() < c2->graceIndex())
                || (c1->isGraceAfter() && c1->graceIndex() > c2->graceIndex()))
                  return c1;
            else
                  return c2;
            }
      }

//---------------------------------------------------------
//   doSlurs
//---------------------------------------------------------

void SlurHandler::doSlurs(const ChordRest* chordRest, Notations& notations, BrfWriter& brf)
      {
      // loop over all slurs twice, first to handle the stops, then the starts
      for (int i = 0; i < 2; ++i) {
            // search for slur(s) starting or stopping at this chord
            for (const auto it : chordRest->score()->spanner()) {
                  auto sp = it.second;
                  if (sp->generated() || sp->type() != ElementType::SLUR)
                        continue;
                  if (chordRest == sp->startElement() || chordRest == sp->endElement()) {
                        const auto s = static_cast<const Slur*>(sp);
                        const auto firstChordRest = findFirstChordRest(s);
                        if (firstChordRest) {
                              if (i == 0) {
                                    // first time: do slur stops
                                    if (firstChordRest != chordRest)
                                          doSlurStop(s, notations, brf);
                                    }
                              else {
                                    // second time: do slur starts
                                    if (firstChordRest == chordRest)
                                          doSlurStart(s, notations, brf);
                                    }
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//   doSlurStart
//---------------------------------------------------------

void SlurHandler::doSlurStart(const Slur* s, Notations& notations, BrfWriter& brf)
      {
      // check if on slur list (i.e. stop already seen)
      int i = findSlur(s);
      // compose tag
      QString tagName = "slur";
      tagName += slurTieLineStyle(s); // define line type
      tagName += color2xml(s);
      tagName += QString(" type=\"start\" placement=\"%1\"")
            .arg(s->up() ? "above" : "below");
      tagName += addPositioningAttributes(s, true);

      if (i >= 0) {
            // remove from list and print start
            slur[i] = 0;
            started[i] = false;
            notations.tag(brf);
            tagName += QString(" number=\"%1\"").arg(i + 1);
            brf.tagE(tagName);
            }
      else {
            // find free slot to store it
            i = findSlur(0);
            if (i >= 0) {
                  slur[i] = s;
                  started[i] = true;
                  notations.tag(brf);
                  tagName += QString(" number=\"%1\"").arg(i + 1);
                  brf.tagE(tagName);
                  }
            else
                  qDebug("no free slur slot");
            }
      }

//---------------------------------------------------------
//   doSlurStop
//---------------------------------------------------------

// Note: a slur may start in a higher voice in the same measure.
// In that case it is not yet started (i.e. on the active slur list)
// when doSlurStop() is executed. Handle this slur as follows:
// - generate stop anyway and put it on the slur list
// - doSlurStart() starts slur but doesn't store it

void SlurHandler::doSlurStop(const Slur* s, Notations& notations, BrfWriter &braille)
      {
      // check if on slur list
      int i = findSlur(s);
      if (i < 0) {
            // if not, find free slot to store it
            i = findSlur(0);
            if (i >= 0) {
                  slur[i] = s;
                  started[i] = false;
                  notations.tag(braille);
                  QString tagName = QString("slur type=\"stop\" number=\"%1\"").arg(i + 1);
                  tagName += addPositioningAttributes(s, false);
                  braille.tagE(tagName);
                  }
            else
                  qDebug("no free slur slot");
            }
      else {
            // found (already started), stop it and remove from list
            slur[i] = 0;
            started[i] = false;
            notations.tag(braille);
            QString tagName = QString("slur type=\"stop\" number=\"%1\"").arg(i + 1);
            tagName += addPositioningAttributes(s, false);
            braille.tagE(tagName);
            }
      }

//---------------------------------------------------------
//   glissando
//---------------------------------------------------------

// <notations>
//   <slide line-type="solid" number="1" type="start"/>
//   </notations>

// <notations>
//   <glissando line-type="wavy" number="1" type="start"/>
//   </notations>

static void glissando(const Glissando* gli, int number, bool start, Notations& notations, BrfWriter& brf)
      {
      GlissandoType st = gli->glissandoType();
      QString tagName;
      switch (st) {
            case GlissandoType::STRAIGHT:
                  tagName = "slide line-type=\"solid\"";
                  break;
            case GlissandoType::WAVY:
                  tagName = "glissando line-type=\"wavy\"";
                  break;
            default:
                  qDebug("unknown glissando subtype %d", int(st));
                  return;
                  break;
            }
      tagName += QString(" number=\"%1\" type=\"%2\"").arg(number).arg(start ? "start" : "stop");
      tagName += color2xml(gli);
      tagName += addPositioningAttributes(gli, start);
      notations.tag(brf);
      if (start && gli->showText() && gli->text() != "")
            brf.tag(tagName, gli->text());
      else
            brf.tagE(tagName);
      }

//---------------------------------------------------------
//   GlissandoHandler
//---------------------------------------------------------

GlissandoHandler::GlissandoHandler()
      {
      for (int i = 0; i < MAX_NUMBER_LEVEL; ++i) {
            glissNote[i] = 0;
            slideNote[i] = 0;
            }
      }

//---------------------------------------------------------
//   findNote -- get index of Note in note table for subtype type
//   return -1 if not found
//---------------------------------------------------------

int GlissandoHandler::findNote(const Note* note, int type) const
      {
      if (type != 0 && type != 1) {
            qDebug("GlissandoHandler::findNote: unknown glissando subtype %d", type);
            return -1;
            }
      for (int i = 0; i < MAX_NUMBER_LEVEL; ++i) {
            if (type == 0 && slideNote[i] == note) return i;
            if (type == 1 && glissNote[i] == note) return i;
            }
      return -1;
      }

//---------------------------------------------------------
//   doGlissandoStart
//---------------------------------------------------------

void GlissandoHandler::doGlissandoStart(Glissando* gliss, Notations& notations, BrfWriter& brf)
      {
      GlissandoType type = gliss->glissandoType();
      if (type != GlissandoType::STRAIGHT && type != GlissandoType::WAVY) {
            qDebug("doGlissandoStart: unknown glissando subtype %d", int(type));
            return;
            }
      Note* note = static_cast<Note*>(gliss->startElement());
      // check if on chord list
      int i = findNote(note, int(type));
      if (i >= 0) {
            // print error and remove from list
            qDebug("doGlissandoStart: note for glissando/slide %p already on list", gliss);
            if (type == GlissandoType::STRAIGHT) slideNote[i] = 0;
            if (type == GlissandoType::WAVY) glissNote[i] = 0;
            }
      // find free slot to store it
      i = findNote(0, int(type));
      if (i >= 0) {
            if (type == GlissandoType::STRAIGHT) slideNote[i] = note;
            if (type == GlissandoType::WAVY) glissNote[i] = note;
            glissando(gliss, i + 1, true, notations, brf);
            }
      else
            qDebug("doGlissandoStart: no free slot");
      }

//---------------------------------------------------------
//   doGlissandoStop
//---------------------------------------------------------

void GlissandoHandler::doGlissandoStop(Glissando* gliss, Notations& notations, BrfWriter& brf)
      {
      GlissandoType type = gliss->glissandoType();
      if (type != GlissandoType::STRAIGHT && type != GlissandoType::WAVY) {
            qDebug("doGlissandoStart: unknown glissando subtype %d", int(type));
            return;
            }
      Note* note = static_cast<Note*>(gliss->startElement());
      for (int i = 0; i < MAX_NUMBER_LEVEL; ++i) {
            if (type == GlissandoType::STRAIGHT && slideNote[i] == note) {
                  slideNote[i] = 0;
                  glissando(gliss, i + 1, false, notations, brf);
                  return;
                  }
            if (type == GlissandoType::WAVY && glissNote[i] == note) {
                  glissNote[i] = 0;
                  glissando(gliss, i + 1, false, notations, brf);
                  return;
                  }
            }
      qDebug("doGlissandoStop: glissando note %p not found", note);
      }

//---------------------------------------------------------
//   directions anchor -- anchor directions at another element or a specific tick
//---------------------------------------------------------

class DirectionsAnchor {
      Element* direct;        // the element containing the direction
      Element* anchor;        // the element it is attached to
      bool start;             // whether it is attached to start or end
      Fraction tick;          // the timestamp

public:
      DirectionsAnchor(Element* a, bool s, const Fraction& t) { direct = 0; anchor = a; start = s; tick = t; }
      DirectionsAnchor(const Fraction& t) { direct = 0; anchor = 0; start = true; tick = t; }
      Element* getDirect() { return direct; }
      Element* getAnchor() { return anchor; }
      bool getStart() { return start; }
      Fraction getTick() { return tick; }
      void setDirect(Element* d) { direct = d; }
      };

//---------------------------------------------------------
// trill handling
//---------------------------------------------------------

// find all trills in this measure and this part

static void findTrills(Measure* measure, int strack, int etrack, TrillHash& trillStart, TrillHash& trillStop)
      {
      // loop over all spanners in this measure
      auto stick = measure->tick();
      auto etick = measure->tick() + measure->ticks();
      for (auto it = measure->score()->spanner().lower_bound(stick.ticks()); it != measure->score()->spanner().upper_bound(etick.ticks()); ++it) {
            auto e = it->second;
            //qDebug("1 trill %p type %d track %d tick %s", e, e->type(), e->track(), qPrintable(e->tick().print()));
            if (e->isTrill() && strack <= e->track() && e->track() < etrack
                && e->tick() >= measure->tick() && e->tick() < (measure->tick() + measure->ticks()))
                  {
                  //qDebug("2 trill %p", e);
                  // a trill is found starting in this segment, trill end time is known
                  // determine notes to write trill start and stop

                  const auto tr = toTrill(e);
                  auto elem1 = tr->startElement();
                  auto elem2 = tr->endElement();

                  if (elem1 && elem1->isChord() && elem2 && elem2->isChord()) {
                        trillStart.insert(toChord(elem1), tr);
                        trillStop.insert(toChord(elem2), tr);
                        }
                  }
            }
      }

//---------------------------------------------------------
// helpers for ::calcDivisions
//---------------------------------------------------------

typedef QList<int> IntVector;
static IntVector integers;
static IntVector primes;

// check if all integers can be divided by d

static bool canDivideBy(int d)
      {
      bool res = true;
      for (int i = 0; i < integers.count(); i++) {
            if ((integers[i] <= 1) || ((integers[i] % d) != 0)) {
                  res = false;
                  }
            }
      return res;
      }

// divide all integers by d

static void divideBy(int d)
      {
      for (int i = 0; i < integers.count(); i++) {
            integers[i] /= d;
            }
      }

static void addInteger(int len)
      {
      if (!integers.contains(len)) {
            integers.append(len);
            }
      }

//---------------------------------------------------------
//   calcDivMoveToTick
//---------------------------------------------------------

void ExportBrf::calcDivMoveToTick(const Fraction& t)
      {
      if (t < _tick) {
#ifdef DEBUG_TICK
            qDebug("backup %d", (tick - t).ticks());
#endif
            addInteger((_tick - t).ticks());
            }
      else if (t > _tick) {
#ifdef DEBUG_TICK
            qDebug("forward %d", (t - tick).ticks());;
#endif
            addInteger((t - _tick).ticks());
            }
      _tick = t;
      }

//---------------------------------------------------------
// isTwoNoteTremolo - determine if chord is part of two note tremolo
//---------------------------------------------------------

static bool isTwoNoteTremolo(Chord* chord)
      {
      return (chord->tremolo() && chord->tremolo()->twoNotes());
      }

//---------------------------------------------------------
//  calcDivisions
//---------------------------------------------------------

// Loop over all voices in all staffs and determine a suitable value for divisions.

// Length of time in MusicXML is expressed in "units", which should allow expressing all time values
// as an integral number of units. Divisions contains the number of units in a quarter note.
// MuseScore uses division (480) midi ticks to represent a quarter note, which expresses all note values
// plus triplets and quintuplets as integer values. Solution is to collect all time values required,
// and divide them by the highest common denominator, which is implemented as a series of
// divisions by prime factors. Initialize the list with division to make sure a quarter note can always
// be written as an integral number of units.

/**
 */

void ExportBrf::calcDivisions()
      {
      // init
      integers.clear();
      primes.clear();
      integers.append(MScore::division);
      primes.append(2);
      primes.append(3);
      primes.append(5);

      const QList<Part*>& il = _score->parts();

      for (int idx = 0; idx < il.size(); ++idx) {

            Part* part = il.at(idx);
            _tick = { 0,1 };

            int staves = part->nstaves();
            int strack = _score->staffIdx(part) * VOICES;
            int etrack = strack + staves * VOICES;

            for (MeasureBase* mb = _score->measures()->first(); mb; mb = mb->next()) {

                  if (mb->type() != ElementType::MEASURE)
                        continue;
                  Measure* m = (Measure*)mb;

                  for (int st = strack; st < etrack; ++st) {
                        // sstaff - xml staff number, counting from 1 for this
                        // instrument
                        // special number 0 -> don’t show staff number in
                        // xml output (because there is only one staff)

                        int sstaff = (staves > 1) ? st - strack + VOICES : 0;
                        sstaff /= VOICES;

                        for (Segment* seg = m->first(); seg; seg = seg->next()) {

                              Element* el = seg->element(st);
                              if (!el)
                                    continue;

                              // must ignore start repeat to prevent spurious backup/forward
                              if (el->type() == ElementType::BAR_LINE && static_cast<BarLine*>(el)->barLineType() == BarLineType::START_REPEAT)
                                    continue;

                              if (_tick != seg->tick())
                                    calcDivMoveToTick(seg->tick());

                              if (el->isChordRest()) {
                                    Fraction l = toChordRest(el)->actualTicks();
                                    if (el->isChord()) {
                                          if (isTwoNoteTremolo(toChord(el)))
                                                l = l * Fraction(1,2);
                                          }
#ifdef DEBUG_TICK
                                    qDebug("chordrest %d", l);
#endif
                                    addInteger(l.ticks());
                                    _tick += l;
                                    }
                              }
                        }
                  // move to end of measure (in case of incomplete last voice)
                  calcDivMoveToTick(m->endTick());
                  }
            }

      // do it: divide by all primes as often as possible
      for (int u = 0; u < primes.count(); u++)
            while (canDivideBy(primes[u]))
                  divideBy(primes[u]);

      div = MScore::division / integers[0];
#ifdef DEBUG_TICK
      qDebug("divisions=%d div=%d", integers[0], div);
#endif
      }

//---------------------------------------------------------
//   writePageFormat
//---------------------------------------------------------

static void writePageFormat(const Score* const s, BrfWriter& brf, double conversion)
      {
      brf.stag("page-layout");

      brf.tag("page-height", s->styleD(Sid::pageHeight) * conversion);
      brf.tag("page-width", s->styleD(Sid::pageWidth) * conversion);

      QString type("both");
      if (s->styleB(Sid::pageTwosided)) {
            type = "even";
            brf.stag(QString("page-margins type=\"%1\"").arg(type));
            brf.tag("left-margin",   s->styleD(Sid::pageEvenLeftMargin) * conversion);
            brf.tag("right-margin",  s->styleD(Sid::pageOddLeftMargin) * conversion);
            brf.tag("top-margin",    s->styleD(Sid::pageEvenTopMargin)  * conversion);
            brf.tag("bottom-margin", s->styleD(Sid::pageEvenBottomMargin) * conversion);
            brf.etag();
            type = "odd";
            }
      brf.stag(QString("page-margins type=\"%1\"").arg(type));
      brf.tag("left-margin",   s->styleD(Sid::pageOddLeftMargin) * conversion);
      brf.tag("right-margin",  s->styleD(Sid::pageEvenLeftMargin) * conversion);
      brf.tag("top-margin",    s->styleD(Sid::pageOddTopMargin) * conversion);
      brf.tag("bottom-margin", s->styleD(Sid::pageOddBottomMargin) * conversion);
      brf.etag();

      brf.etag();
      }

//---------------------------------------------------------
//   defaults
//---------------------------------------------------------

// _spatium = DPMM * (millimeter * 10.0 / tenths);

static void defaults(BrfWriter& brf, const Score* const s, double& millimeters, const int& tenths)
      {
      brf.stag("defaults");
      brf.stag("scaling");
      brf.tag("millimeters", millimeters);
      brf.tag("tenths", tenths);
      brf.etag();

      writePageFormat(s, brf, INCH / millimeters * tenths);

      // TODO: also write default system layout here
      // when exporting only manual or no breaks, system-distance is not written at all

      // font defaults
      // as MuseScore supports dozens of different styles, while MusicXML only has defaults
      // for music (TODO), words and lyrics, use Tid STAFF (typically used for words)
      // and LYRIC1 to get MusicXML defaults

      // TODO xml.tagE("music-font font-family=\"TBD\" font-size=\"TBD\"");
      brf.tagE(QString("word-font font-family=\"%1\" font-size=\"%2\"").arg(s->styleSt(Sid::staffTextFontFace)).arg(s->styleD(Sid::staffTextFontSize)));
      brf.tagE(QString("lyric-font font-family=\"%1\" font-size=\"%2\"").arg(s->styleSt(Sid::lyricsOddFontFace)).arg(s->styleD(Sid::lyricsOddFontSize)));
      brf.etag();
      }


//---------------------------------------------------------
//   creditWords
//---------------------------------------------------------

static void creditWords(BrfWriter& brf, Score* s, double x, double y, QString just, QString val, const QList<TextFragment>& words)
      {
      // prevent incorrect MusicXML for empty text
      if (words.isEmpty())
            return;

      const QString mtf = s->styleSt(Sid::MusicalTextFont);
      CharFormat defFmt;
      defFmt.setFontFamily(s->styleSt(Sid::staffTextFontFace));
      defFmt.setFontSize(s->styleD(Sid::staffTextFontSize));

      // export formatted
      brf.stag("credit page=\"1\"");
      QString attr = QString(" default-x=\"%1\"").arg(x);
      attr += QString(" default-y=\"%1\"").arg(y);
      attr += " justify=\"" + just + "\"";
      attr += " valign=\"" + val + "\"";
      MScoreTextToBraille mttm("credit-words", attr, defFmt, mtf); // will need to change this function
      mttm.writeTextFragments(words, brf);
      brf.etag();
      }

//---------------------------------------------------------
//   parentHeight
//---------------------------------------------------------

static double parentHeight(const Element* element)
      {
      const Element* parent = element->parent();

      if (!parent)
            return 0;

      if (parent->type() == ElementType::VBOX) {
            return parent->height();
            }

      return 0;
      }

//---------------------------------------------------------
//   credits
//---------------------------------------------------------

void ExportBrf::credits(BrfWriter& brf)
      {
      // determine page formatting
      const double h  = getTenthsFromInches(_score->styleD(Sid::pageHeight));
      const double w  = getTenthsFromInches(_score->styleD(Sid::pageWidth));
      const double lm = getTenthsFromInches(_score->styleD(Sid::pageOddLeftMargin));
      const double rm = getTenthsFromInches(_score->styleD(Sid::pageEvenLeftMargin));
      //const double tm = getTenthsFromInches(_score->styleD(Sid::pageOddTopMargin));
      const double bm = getTenthsFromInches(_score->styleD(Sid::pageOddBottomMargin));
      //qDebug("page h=%g w=%g lm=%g rm=%g tm=%g bm=%g", h, w, lm, rm, tm, bm);

      // write the credits
      const MeasureBase* measure = _score->measures()->first();
      if (measure) {
            for (const Element* element : measure->el()) {
                  if (element->isText()) {
                        const Text* text = toText(element);
                        const double ph = getTenthsFromDots(parentHeight(text));

                        double tx = w / 2;
                        double ty = h - getTenthsFromDots(text->pagePos().y());

                        Align al = text->align();
                        QString just;
                        QString val;

                        if (al & Align::RIGHT) {
                              just = "right";
                              tx   = w - rm;
                              }
                        else if (al & Align::HCENTER) {
                              just = "center";
                              // tx already set correctly
                              }
                        else {
                              just = "left";
                              tx   = lm;
                              }

                        if (al & Align::BOTTOM) {
                              val = "bottom";
                              ty -= ph;
                              }
                        else if (al & Align::VCENTER) {
                              val = "middle";
                              ty -= ph / 2;
                              }
                        else if (al & Align::BASELINE) {
                              val = "baseline";
                              ty -= ph / 2;
                              }
                        else {
                              val = "top";
                              // ty already set correctly
                              }

                        creditWords(brf, _score, tx, ty, just, val, text->fragmentList());
                        }
                  }
            }

      const QString rights = _score->metaTag("copyright");
      if (!rights.isEmpty()) {
            // put copyright at the bottom center of the page
            // note: as the copyright metatag contains plain text, special XML characters must be escaped
            TextFragment f(BrfWriter::brfString(rights));
            f.changeFormat(FormatId::FontFamily, _score->styleSt(Sid::footerFontFace));
            f.changeFormat(FormatId::FontSize, _score->styleD(Sid::footerFontSize));
            QList<TextFragment> list;
            list.append(f);
            creditWords(brf, _score, w / 2, bm, "center", "bottom", list);
            }
      }

//---------------------------------------------------------
//   midipitch2xml
//---------------------------------------------------------

static int alterTab[12] = { 0,   1,   0,   1,   0,  0,   1,   0,   1,   0,   1,   0 };
static char noteTab[12] = { 'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B' };

static void midipitch2xml(int pitch, char& c, int& alter, int& octave)
      {
      // 60 = C 4
      c      = noteTab[pitch % 12];
      alter  = alterTab[pitch % 12];
      octave = pitch / 12 - 1;
      //qDebug("midipitch2xml(pitch %d) step %c, alter %d, octave %d", pitch, c, alter, octave);
      }

//---------------------------------------------------------
//   tabpitch2xml
//---------------------------------------------------------

static void tabpitch2xml(const int pitch, const int tpc, QString& s, int& alter, int& octave)
      {
      s      = tpc2stepName(tpc);
      alter  = tpc2alterByKey(tpc, Key::C);
      octave = (pitch - alter) / 12 - 1;
      if (alter < -2 || 2 < alter)
            qDebug("tabpitch2xml(pitch %d, tpc %d) problem:  step %s, alter %d, octave %d",
                   pitch, tpc, qPrintable(s), alter, octave);
      /*
      else
            qDebug("tabpitch2xml(pitch %d, tpc %d) step %s, alter %d, octave %d",
                   pitch, tpc, qPrintable(s), alter, octave);
       */
      }

//---------------------------------------------------------
//   pitch2xml
//---------------------------------------------------------

// TODO validation

static void pitch2xml(const Note* note, QString& s, int& alter, int& octave)
      {

      const Staff* st = note->staff();
      const Instrument* instr = st->part()->instrument();   // TODO: tick
      const Interval intval = instr->transpose();

      s      = tpc2stepName(note->tpc());
      alter  = tpc2alterByKey(note->tpc(), Key::C);
      // note that pitch must be converted to concert pitch
      // in order to calculate the correct octave
      octave = (note->pitch() - intval.chromatic - alter) / 12 - 1;

      //
      // HACK:
      // On percussion clefs there is no relationship between
      // note->pitch() and note->line()
      // note->line() is determined by drumMap
      //
      Fraction tick        = note->chord()->tick();
      ClefType ct     = st->clef(tick);
      if (ct == ClefType::PERC || ct == ClefType::PERC2) {
            alter = 0;
            octave = line2pitch(note->line(), ct, Key::C) / 12 - 1;
            }

      // correct for ottava lines
      int ottava = 0;
      switch (note->ppitch() - note->pitch()) {
            case  24: ottava =  2; break;
            case  12: ottava =  1; break;
            case   0: ottava =  0; break;
            case -12: ottava = -1; break;
            case -24: ottava = -2; break;
            default:  qDebug("pitch2xml() tick=%d pitch()=%d ppitch()=%d",
                             tick.ticks(), note->pitch(), note->ppitch());
            }
      octave += ottava;

      //qDebug("pitch2xml(pitch %d, tpc %d, ottava %d clef %hhd) step    %s, alter    %d, octave    %d",
      //       note->pitch(), note->tpc(), ottava, clef, qPrintable(s), alter, octave);
      }

// unpitch2xml -- calculate display-step and display-octave for an unpitched note
// note:
// even though this produces the correct step/octave according to Recordare's tutorial
// Finale Notepad 2012 does not import a three line staff with percussion clef correctly
// Same goes for Sibelius 6 in case of three or five line staff with percussion clef

static void unpitch2xml(const Note* note, QString& s, int& octave)
      {
      static char table1[]  = "FEDCBAG";

      Fraction tick        = note->chord()->tick();
      Staff* st       = note->staff();
      ClefType ct     = st->clef(tick);
      // offset in lines between staff with current clef and with G clef
      int clefOffset  = ClefInfo::pitchOffset(ct) - ClefInfo::pitchOffset(ClefType::G);
      // line note would be on on a five line staff with G clef
      // note top line is line 0, bottom line is line 8
      int line5g      = note->line() - clefOffset;
      // in MusicXML with percussion clef, step and octave are determined as if G clef is used
      // when stafflines is not equal to five, in MusicXML the bottom line is still E4.
      // in MuseScore assumes line 0 is F5
      // MS line numbers (top to bottom) plus correction to get lowest line at E4 (line 8)
      // 1 line staff: 0             -> correction 8
      // 3 line staff: 2, 4, 6       -> correction 2
      // 5 line staff: 0, 2, 4, 6, 8 -> correction 0
      // TODO handle other # staff lines ?
      if (st->lines(Fraction(0,1)) == 1) line5g += 8;
      if (st->lines(Fraction(0,1)) == 3) line5g += 2;
      // index in table1 to get step
      int stepIdx     = (line5g + 700) % 7;
      // get step
      s               = table1[stepIdx];
      // calculate octave, offset "3" correcting for the fact that an octave starts
      // with C instead of F
      octave =(3 - line5g + 700) / 7 + 5 - 100;
      // qDebug("ExportMusicXml::unpitch2xml(%p) clef %d clef.po %d clefOffset %d staff.lines %d note.line %d line5g %d step %c oct %d",
      //        note, ct, clefTable[ct].pitchOffset, clefOffset, st->lines(), note->line(), line5g, step, octave);
      }

//---------------------------------------------------------
//   tick2xml
//    set type + dots depending on tick len
//---------------------------------------------------------

static QString tick2xml(const Fraction& ticks, int* dots)
      {
      TDuration t;
      t.setVal(ticks.ticks());
      *dots = t.dots();
      return t.name();
      }

//---------------------------------------------------------
//   findVolta -- find volta starting in measure m
//---------------------------------------------------------

static Volta* findVolta(Measure* m, bool left)
      {
      Fraction stick = m->tick();
      Fraction etick = m->tick() + m->ticks();
      auto spanners = m->score()->spannerMap().findOverlapping(stick.ticks(), etick.ticks());
      for (auto i : spanners) {
            Spanner* el = i.value;
            if (el->type() != ElementType::VOLTA)
                  continue;
            if (left && el->tick() == stick)
                  return (Volta*) el;
            if (!left && el->tick2() == etick)
                  return (Volta*) el;
            }
      return 0;
      }

//---------------------------------------------------------
//   ending
//---------------------------------------------------------

static void ending(BrfWriter& brf, Volta* v, bool left)
      {
      QString number = "";
      QString type = "";
      for (int i : v->endings()) {
            if (!number.isEmpty())
                  number += ", ";
            number += QString("%1").arg(i);
            }
      if (left) {
            type = "start";
            }
      else {
            Volta::Type st = v->voltaType();
            switch (st) {
                  case Volta::Type::OPEN:
                        type = "discontinue";
                        break;
                  case Volta::Type::CLOSED:
                        type = "stop";
                        break;
                  default:
                        qDebug("unknown volta subtype %d", int(st));
                        return;
                  }
            }
      QString voltaBrf = QString("ending number=\"%1\" type=\"%2\"").arg(number).arg(type);
      voltaBrf += addPositioningAttributes(v, left);
      brf.tagE(voltaBrf);
      }

//---------------------------------------------------------
//   barlineLeft -- search for and handle barline left
//---------------------------------------------------------

void ExportBrf::barlineLeft(Measure* m)
      {
      bool rs = m->repeatStart();
      Volta* volta = findVolta(m, true);
      if (!rs && !volta) return;
      _attr.doAttr(_brf, false);
      _brf.stag(QString("barline location=\"left\""));
      if (rs)
            _brf.tag("bar-style", QString("heavy-light"));
      if (volta)
            ending(_brf, volta, true);
      if (rs)
            _brf.tagE("repeat direction=\"forward\"");
      _brf.etag();
      }

//---------------------------------------------------------
//   shortBarlineStyle -- recognize normal but shorter barline styles
//---------------------------------------------------------

static QString shortBarlineStyle(const BarLine* bl)
      {
      if (bl->barLineType() == BarLineType::NORMAL && !bl->spanStaff()) {
            if (bl->spanTo() < 0) {
                  // lowest point of barline above lowest staff line
                  if (bl->spanFrom() < 0) {
                        return "tick";       // highest point of barline above highest staff line
                        }
                  else
                        return "short";       // highest point of barline below highest staff line
                  }
            }

      return "";
      }

//---------------------------------------------------------
//   normalBarlineStyle -- recognize other barline styles
//---------------------------------------------------------

static QString normalBarlineStyle(const BarLine* bl)
      {
      const auto bst = bl->barLineType();

      switch (bst) {
            case BarLineType::NORMAL:
                  return "regular";
            case BarLineType::DOUBLE:
                  return "light-light";
            case BarLineType::END_REPEAT:
                  return "light-heavy";
            case BarLineType::BROKEN:
                  return "dashed";
            case BarLineType::DOTTED:
                  return "dotted";
            case BarLineType::END:
            case BarLineType::END_START_REPEAT:
                  return "light-heavy";
            default:
                  qDebug("bar subtype %d not supported", int(bst));
            }

      return "";
      }

//---------------------------------------------------------
//   barlineMiddle -- handle barline middle
//---------------------------------------------------------

void ExportBrf::barlineMiddle(const BarLine* bl)
      {
      auto vis = bl->visible();
      auto shortStyle = shortBarlineStyle(bl);
      auto normalStyle = normalBarlineStyle(bl);
      QString barStyle;
      if (!vis)
            barStyle = "none";
      else if (shortStyle != "")
            barStyle = shortStyle;
      else
            barStyle = normalStyle;

      if (barStyle != "") {
            _brf.stag(QString("barline location=\"middle\""));
            _brf.tag("bar-style", barStyle);
            _brf.etag();
            }
      }

//---------------------------------------------------------
//   barlineRight -- search for and handle barline right
//---------------------------------------------------------

void ExportBrf::barlineRight(Measure* m)
      {
      const Measure* mmR1 = m->mmRest1(); // the multi measure rest this measure is covered by
      const Measure* mmRLst = mmR1->isMMRest() ? mmR1->mmRestLast() : 0; // last measure of replaced sequence of empty measures
      // note: use barlinetype as found in multi measure rest for last measure of replaced sequence
      BarLineType bst = m == mmRLst ? mmR1->endBarLineType() : m->endBarLineType();
      bool visible = m->endBarLineVisible();

      bool needBarStyle = (bst != BarLineType::NORMAL && bst != BarLineType::START_REPEAT) || !visible;
      Volta* volta = findVolta(m, false);
      // detect short and tick barlines
      QString special = "";
      if (bst == BarLineType::NORMAL) {
            const BarLine* bl = m->endBarLine();
            if (bl && !bl->spanStaff()) {
                  if (bl->spanFrom() == BARLINE_SPAN_TICK1_FROM && bl->spanTo() == BARLINE_SPAN_TICK1_TO)
                        special = "tick";
                  if (bl->spanFrom() == BARLINE_SPAN_TICK2_FROM && bl->spanTo() == BARLINE_SPAN_TICK2_TO)
                        special = "tick";
                  if (bl->spanFrom() == BARLINE_SPAN_SHORT1_FROM && bl->spanTo() == BARLINE_SPAN_SHORT1_TO)
                        special = "short";
                  if (bl->spanFrom() == BARLINE_SPAN_SHORT2_FROM && bl->spanTo() == BARLINE_SPAN_SHORT2_FROM)
                        special = "short";
                  }
            }
      if (!needBarStyle && !volta && special.isEmpty())
            return;

      _brf.stag(QString("barline location=\"right\""));
      if (needBarStyle) {
            if (!visible) {
                  _brf.tag("bar-style", QString("none"));
                  }
            else {
                  switch (bst) {
                        case BarLineType::DOUBLE:
                              _brf.tag("bar-style", QString("light-light"));
                              break;
                        case BarLineType::END_REPEAT:
                              _brf.tag("bar-style", QString("light-heavy"));
                              break;
                        case BarLineType::BROKEN:
                              _brf.tag("bar-style", QString("dashed"));
                              break;
                        case BarLineType::DOTTED:
                              _brf.tag("bar-style", QString("dotted"));
                              break;
                        case BarLineType::END:
                        case BarLineType::END_START_REPEAT:
                              _brf.tag("bar-style", QString("light-heavy"));
                              break;
                        default:
                              qDebug("ExportBrf::bar(): bar subtype %d not supported", int(bst));
                              break;
                        }
                  }
            }
      else if (!special.isEmpty()) {
            _brf.tag("bar-style", special);
            }

      if (volta) {
            ending(_brf, volta, false);
            }

      if (bst == BarLineType::END_REPEAT || bst == BarLineType::END_START_REPEAT) {
            if (m->repeatCount() > 2) {
                  _brf.tagE(QString("repeat direction=\"backward\" times=\"%1\"").arg(m->repeatCount()));
                  } else {
                  _brf.tagE("repeat direction=\"backward\"");
                  }
            }

      _brf.etag();
      }

//---------------------------------------------------------
//   moveToTick
//---------------------------------------------------------

void ExportBrf::moveToTick(const Fraction& t)
      {
      // qDebug("ExportBrf::moveToTick(t=%d) tick=%d", t, tick);
      if (t < _tick) {
#ifdef DEBUG_TICK
            qDebug(" -> backup");
#endif
            _attr.doAttr(_brf, false);
            _brf.stag("backup");
            _brf.tag("duration", (_tick - t).ticks() / div);
            _brf.etag();
            }
      else if (t > _tick) {
#ifdef DEBUG_TICK
            qDebug(" -> forward");
#endif
            _attr.doAttr(_brf, false);
            _brf.stag("forward");
            _brf.tag("duration", (t - _tick).ticks() / div);
            _brf.etag();
            }
      _tick = t;
      }

//---------------------------------------------------------
//   timesig
//---------------------------------------------------------

void ExportBrf::timesig(TimeSig* tsig)
      {
      TimeSigType st = tsig->timeSigType();
      Fraction ts = tsig->sig();
      int z = ts.numerator();
      int n = ts.denominator();
      QString ns = tsig->numeratorString();

      _attr.doAttr(_brf, true);
      QString tagName = "time";
      if (st == TimeSigType::FOUR_FOUR)
            tagName += " symbol=\"common\"";
      else if (st == TimeSigType::ALLA_BREVE)
            tagName += " symbol=\"cut\"";
      if (!tsig->visible())
            tagName += " print-object=\"no\"";
      tagName += color2xml(tsig);
      _brf.stag(tagName);

      QRegExp rx("^\\d+(\\+\\d+)+$"); // matches a compound numerator
      if (rx.exactMatch(ns))
            // if compound numerator, exported as is
            _brf.tag("beats", ns);
      else
            // else fall back and use the numerator as integer
            _brf.tag("beats", z);
      _brf.tag("beat-type", n);
      _brf.etag();
      }

//---------------------------------------------------------
//   accSymId2alter
//---------------------------------------------------------

static double accSymId2alter(SymId id)
      {
      double res = 0;
      switch (id) {
            case SymId::accidentalDoubleFlat:                      res = -2;   break;
            case SymId::accidentalThreeQuarterTonesFlatZimmermann: res = -1.5; break;
            case SymId::accidentalFlat:                            res = -1;   break;
            case SymId::accidentalQuarterToneFlatStein:            res = -0.5; break;
            case SymId::accidentalNatural:                         res =  0;   break;
            case SymId::accidentalQuarterToneSharpStein:           res =  0.5; break;
            case SymId::accidentalSharp:                           res =  1;   break;
            case SymId::accidentalThreeQuarterTonesSharpStein:     res =  1.5; break;
            case SymId::accidentalDoubleSharp:                     res =  2;   break;
            default: qDebug("accSymId2alter: unsupported sym %s", Sym::id2name(id));
            }
      return res;
      }

//---------------------------------------------------------
//   keysig
//---------------------------------------------------------

void ExportBrf::keysig(const KeySig* ks, ClefType ct, int staff, bool visible)
      {
      static char table2[]  = "CDEFGAB";
      int po = ClefInfo::pitchOffset(ct); // actually 7 * oct + step for topmost staff line
      //qDebug("keysig st %d key %d custom %d ct %hhd st %d", staff, kse.key(), kse.custom(), ct, staff);
      //qDebug(" pitch offset clef %d stp %d oct %d ", po, po % 7, po / 7);

      QString tagName = "key";
      if (staff)
            tagName += QString(" number=\"%1\"").arg(staff);
      if (!visible)
            tagName += " print-object=\"no\"";
      tagName += color2xml(ks);
      _attr.doAttr(_brf, true);
      _brf.stag(tagName);

      const KeySigEvent kse = ks->keySigEvent();
      const QList<KeySym> keysyms = kse.keySymbols();
      if (kse.custom() && !kse.isAtonal() && keysyms.size() > 0) {

            // non-traditional key signature
            // MusicXML order is left-to-right order, while KeySims in keySymbols()
            // are in insertion order -> sorting required

            // first put the KeySyms in a map
            QMap<qreal, KeySym> map;
            for (const KeySym& ksym : keysyms) {
                  map.insert(ksym.spos.x(), ksym);
                  }
            // then write them (automatically sorted on key)
            for (const KeySym& ksym : map) {
                  int line = static_cast<int>(round(2 * ksym.spos.y()));
                  int step = (po - line) % 7;
                  //qDebug(" keysym sym %d spos %g,%g pos %g,%g -> line %d step %d",
                  //       ksym.sym, ksym.spos.x(), ksym.spos.y(), ksym.pos.x(), ksym.pos.y(), line, step);
                  _brf.tag("key-step", QString(QChar(table2[step])));
                  _brf.tag("key-alter", accSymId2alter(ksym.sym));
                  _brf.tag("key-accidental", accSymId2MxmlString(ksym.sym));
                  }
            }
      else {
            // traditional key signature
            _brf.tag("fifths", static_cast<int>(kse.key()));
            switch (kse.mode()) {
                  case KeyMode::NONE:       _brf.tag("mode", "none"); break;
                  case KeyMode::MAJOR:      _brf.tag("mode", "major"); break;
                  case KeyMode::MINOR:      _brf.tag("mode", "minor"); break;
                  case KeyMode::DORIAN:     _brf.tag("mode", "dorian"); break;
                  case KeyMode::PHRYGIAN:   _brf.tag("mode", "phrygian"); break;
                  case KeyMode::LYDIAN:     _brf.tag("mode", "lydian"); break;
                  case KeyMode::MIXOLYDIAN: _brf.tag("mode", "mixolydian"); break;
                  case KeyMode::AEOLIAN:    _brf.tag("mode", "aeolian"); break;
                  case KeyMode::IONIAN:     _brf.tag("mode", "ionian"); break;
                  case KeyMode::LOCRIAN:    _brf.tag("mode", "locrian"); break;
                  case KeyMode::UNKNOWN:    // fall thru
                  default:
                        if (kse.custom())
                              _brf.tag("mode", "none");
                  }
            }
      _brf.etag();
      }

//---------------------------------------------------------
//   clef
//---------------------------------------------------------

void ExportBrf::clef(int staff, const ClefType ct, const QString& extraAttributes)
      {
      clefDebug("ExportBrf::clef(staff %d, clef %hhd)", staff, ct);

      QString tagName = "clef";
      if (staff)
            tagName += QString(" number=\"%1\"").arg(staff);
      tagName += extraAttributes;
      _attr.doAttr(_brf, true);
      _brf.stag(tagName);

      QString sign = ClefInfo::sign(ct);
      int line   = ClefInfo::line(ct);
      _brf.tag("sign", sign);
      _brf.tag("line", line);
      if (ClefInfo::octChng(ct))
            _brf.tag("clef-octave-change", ClefInfo::octChng(ct));
      _brf.etag();
      }

//---------------------------------------------------------
//   tupletStartStop
//---------------------------------------------------------

// LVIFIX: add placement to tuplet support
// <notations>
//   <tuplet type="start" placement="above" bracket="no"/>
// </notations>

static void tupletStartStop(ChordRest* cr, Notations& notations, BrfWriter& brf)
      {
      Tuplet* t = cr->tuplet();
      if (!t) return;
      if (cr == t->elements().front()) {
            notations.tag(brf);
            QString tupletTag = "tuplet type=\"start\"";
            tupletTag += " bracket=";
            tupletTag += t->hasBracket() ? "\"yes\"" : "\"no\"";
            if (t->numberType() == TupletNumberType::SHOW_RELATION)
                  tupletTag += " show-number=\"both\"";
            if (t->numberType() == TupletNumberType::NO_TEXT)
                  tupletTag += " show-number=\"none\"";
            brf.tagE(tupletTag);
            }
      if (cr == t->elements().back()) {
            notations.tag(brf);
            brf.tagE("tuplet type=\"stop\"");
            }
      }

//---------------------------------------------------------
//   findTrill -- get index of trill in trill table
//   return -1 if not found
//---------------------------------------------------------

int ExportBrf::findTrill(const Trill* tr) const
      {
      for (int i = 0; i < MAX_NUMBER_LEVEL; ++i)
            if (trills[i] == tr) return i;
      return -1;
      }

//---------------------------------------------------------
//   writeAccidental
//---------------------------------------------------------

static void writeAccidental(BrfWriter& brf, const QString& tagName, const Accidental* const acc)
      {
      if (acc) {
            QString s = accidentalType2MxmlString(acc->accidentalType());
            if (s != "") {
                  QString tag = tagName;
                  if (acc->bracket() != AccidentalBracket::NONE)
                        tag += " parentheses=\"yes\"";
                  brf.tag(tag, s);
                  }
            }
      }

//---------------------------------------------------------
//   wavyLineStart
//---------------------------------------------------------

static void wavyLineStart(const Trill* tr, const int number, Notations& notations, Ornaments& ornaments, BrfWriter& brf)
      {
      // mscore only supports wavy-line with trill-mark
      notations.tag(brf);
      ornaments.tag(brf);
      brf.tagE("trill-mark");
      writeAccidental(brf, "accidental-mark", tr->accidental());
      QString tagName = "wavy-line type=\"start\"";
      tagName += QString(" number=\"%1\"").arg(number + 1);
      tagName += color2xml(tr);
      tagName += addPositioningAttributes(tr, true);
      brf.tagE(tagName);
      }

//---------------------------------------------------------
//   wavyLineStop
//---------------------------------------------------------

static void wavyLineStop(const Trill* tr, const int number, Notations& notations, Ornaments& ornaments, BrfWriter& brf)
      {
      notations.tag(brf);
      ornaments.tag(brf);
      QString trillBrf = QString("wavy-line type=\"stop\" number=\"%1\"").arg(number + 1);
      trillBrf += addPositioningAttributes(tr, false);
      brf.tagE(trillBrf);
      }

//---------------------------------------------------------
//   wavyLineStartStop
//---------------------------------------------------------

void ExportBrf::wavyLineStartStop(Chord* chord, Notations& notations, Ornaments& ornaments,
                                       TrillHash& trillStart, TrillHash& trillStop)
      {
      if (trillStart.contains(chord) && trillStop.contains(chord)) {
            const auto tr = trillStart.value(chord);
            auto n = findTrill(0);
            if (n >= 0) {
                  wavyLineStart(tr, n, notations, ornaments, _brf);
                  wavyLineStop(tr, n, notations, ornaments, _brf);
                  }
            else
                  qDebug("too many overlapping trills (chord %p staff %d tick %d)",
                         chord, chord->staffIdx(), chord->tick().ticks());
            }
      else {
            if (trillStop.contains(chord)) {
                  const auto tr = trillStop.value(chord);
                  auto n = findTrill(tr);
                  if (n >= 0)
                        // trill stop after trill start
                        trills[n] = 0;
                  else {
                        // trill stop before trill start
                        n = findTrill(0);
                        if (n >= 0)
                              trills[n] = tr;
                        else
                              qDebug("too many overlapping trills (chord %p staff %d tick %d)",
                                     chord, chord->staffIdx(), chord->tick().ticks());
                        }
                  if (n >= 0) {
                        wavyLineStop(tr, n, notations, ornaments, _brf);
                        }
                  trillStop.remove(chord);
                  }
            if (trillStart.contains(chord)) {
                  const auto tr = trillStart.value(chord);
                  auto n = findTrill(tr);
                  if (n >= 0)
                        qDebug("wavyLineStartStop error");
                  else {
                        n = findTrill(0);
                        if (n >= 0) {
                              trills[n] = tr;
                              wavyLineStart(tr, n, notations, ornaments, _brf);
                              }
                        else
                              qDebug("too many overlapping trills (chord %p staff %d tick %d)",
                                     chord, chord->staffIdx(), chord->tick().ticks());
                        trillStart.remove(chord);
                        }
                  }
            }
      }

//---------------------------------------------------------
//   hasBreathMark - determine if chord has breath-mark
//---------------------------------------------------------

static Breath* hasBreathMark(Chord* ch)
      {
      Fraction tick = ch->tick() + ch->actualTicks();
      Segment* s = ch->measure()->findSegment(SegmentType::Breath, tick);
      return s ? toBreath(s->element(ch->track())) : 0;
      }

//---------------------------------------------------------
//   tremoloSingleStartStop
//---------------------------------------------------------

static void tremoloSingleStartStop(Chord* chord, Notations& notations, Ornaments& ornaments, BrfWriter& brf)
      {
      if (chord->tremolo()) {
            Tremolo* tr = chord->tremolo();
            int count = 0;
            TremoloType st = tr->tremoloType();
            QString type = "";

            if (chord->tremoloChordType() == TremoloChordType::TremoloSingle) {
                  type = "single";
                  switch (st) {
                        case TremoloType::R8:  count = 1; break;
                        case TremoloType::R16: count = 2; break;
                        case TremoloType::R32: count = 3; break;
                        case TremoloType::R64: count = 4; break;
                        default: qDebug("unknown tremolo single %d", int(st)); break;
                        }
                  }
            else if (chord->tremoloChordType() == TremoloChordType::TremoloFirstNote) {
                  type = "start";
                  switch (st) {
                        case TremoloType::C8:  count = 1; break;
                        case TremoloType::C16: count = 2; break;
                        case TremoloType::C32: count = 3; break;
                        case TremoloType::C64: count = 4; break;
                        default: qDebug("unknown tremolo double %d", int(st)); break;
                        }
                  }
            else if (chord->tremoloChordType() == TremoloChordType::TremoloSecondNote) {
                  type = "stop";
                  switch (st) {
                        case TremoloType::C8:  count = 1; break;
                        case TremoloType::C16: count = 2; break;
                        case TremoloType::C32: count = 3; break;
                        case TremoloType::C64: count = 4; break;
                        default: qDebug("unknown tremolo double %d", int(st)); break;
                        }
                  }
            else qDebug("unknown tremolo subtype %d", int(st));


            if (type != "" && count > 0) {
                  notations.tag(brf);
                  ornaments.tag(brf);
                  QString tagName = "tremolo";
                  tagName += QString(" type=\"%1\"").arg(type);
                  if (type == "single" || type == "start")
                        tagName += color2xml(tr);
                  brf.tag(tagName, count);
                  }
            }
      }


//---------------------------------------------------------
//   fermatas
//---------------------------------------------------------

static void fermatas(const QVector<Element*>& cra, BrfWriter& brf, Notations& notations)
      {
      for (const Element* e : cra) {
            if (!e->isFermata())
                  continue;
            const Fermata* a = toFermata(e);
            notations.tag(brf);
            QString tagName = "fermata";
            tagName += QString(" type=\"%1\"").arg(a->placement() == Placement::ABOVE ? "upright" : "inverted");
            tagName += color2xml(a);
            SymId id = a->symId();
            if (id == SymId::fermataAbove || id == SymId::fermataBelow)
                  brf.tagE(tagName);
            // MusicXML does not support the very short fermata nor short fermata (Henze),
            // export as short fermata (better than not exporting at all)
            else if (id == SymId::fermataShortAbove || id == SymId::fermataShortBelow
                     || id == SymId::fermataShortHenzeAbove || id == SymId::fermataShortHenzeBelow
                     || id == SymId::fermataVeryShortAbove || id == SymId::fermataVeryShortBelow)
                  brf.tag(tagName, "angled");
            // MusicXML does not support the very long fermata  nor long fermata (Henze),
            // export as long fermata (better than not exporting at all)
            else if (id == SymId::fermataLongAbove || id == SymId::fermataLongBelow
                     || id == SymId::fermataLongHenzeAbove || id == SymId::fermataLongHenzeBelow
                     || id == SymId::fermataVeryLongAbove || id == SymId::fermataVeryLongBelow)
                  brf.tag(tagName, "square");
            }
      }

//---------------------------------------------------------
//   symIdToArtic
//---------------------------------------------------------

static QString symIdToArtic(const SymId sid)
      {
      switch (sid) {
            case SymId::articAccentAbove:
            case SymId::articAccentBelow:
                  return "accent";
                  break;

            case SymId::articStaccatoAbove:
            case SymId::articStaccatoBelow:
            case SymId::articAccentStaccatoAbove:
            case SymId::articAccentStaccatoBelow:
            case SymId::articMarcatoStaccatoAbove:
            case SymId::articMarcatoStaccatoBelow:
                  return "staccato";
                  break;

            case SymId::articStaccatissimoAbove:
            case SymId::articStaccatissimoBelow:
            case SymId::articStaccatissimoStrokeAbove:
            case SymId::articStaccatissimoStrokeBelow:
            case SymId::articStaccatissimoWedgeAbove:
            case SymId::articStaccatissimoWedgeBelow:
                  return "staccatissimo";
                  break;

            case SymId::articTenutoAbove:
            case SymId::articTenutoBelow:
                  return "tenuto";
                  break;

            case SymId::articMarcatoAbove:
            case SymId::articMarcatoBelow:
                  return "strong-accent";
                  break;

            case SymId::articTenutoStaccatoAbove:
            case SymId::articTenutoStaccatoBelow:
                  return "detached-legato";
                  break;

            default:
                  ;       // nothing
                  break;
            }

      return "";
      }

//---------------------------------------------------------
//   symIdToOrnam
//---------------------------------------------------------

static QString symIdToOrnam(const SymId sid)
      {
      switch (sid) {
            case SymId::ornamentTurnInverted:
                  return "inverted-turn";
                  break;
            case SymId::ornamentTurn:
                  return "turn";
                  break;
            case SymId::ornamentTrill:
                  return "trill-mark";
                  break;
            case SymId::ornamentMordentInverted:
                  return "mordent";
                  // return "inverted-mordent";
                  break;
            case SymId::ornamentMordent:
                  // return "mordent";
                  return "inverted-mordent";
                  break;
            case SymId::ornamentTremblement:
                  return "inverted-mordent long=\"yes\"";
                  break;
            case SymId::ornamentPrallMordent:
                  return "mordent long=\"yes\"";
                  break;
            case SymId::ornamentUpPrall:
                  return "inverted-mordent long=\"yes\" approach=\"below\"";
                  break;
            case SymId::ornamentPrecompMordentUpperPrefix:
                  return "inverted-mordent long=\"yes\" approach=\"above\"";
                  break;
            case SymId::ornamentUpMordent:
                  return "mordent long=\"yes\" approach=\"below\"";
                  break;
            case SymId::ornamentDownMordent:
                  return "mordent long=\"yes\" approach=\"above\"";
                  break;
            case SymId::ornamentPrallDown:
                  return "inverted-mordent long=\"yes\" departure=\"below\"";
                  break;
            case SymId::ornamentPrallUp:
                  return "inverted-mordent long=\"yes\" departure=\"above\"";
                  break;
            case SymId::ornamentLinePrall:
                  // MusicXML 3.0 does not distinguish between downprall and lineprall
                  return "inverted-mordent long=\"yes\" approach=\"above\"";
                  break;
            case SymId::ornamentPrecompSlide:
                  return "schleifer";
                  break;

            default:
                  ; // nothing
                  break;
            }

      return "";
      }

//---------------------------------------------------------
//   symIdToTechn
//---------------------------------------------------------

static QString symIdToTechn(const SymId sid)
      {
      switch (sid) {
            case SymId::brassMuteClosed:
                  return "stopped";
                  break;
            case SymId::stringsHarmonic:
                  return "x"; // will be overruled but must be non-empty
                  break;
            case SymId::stringsUpBow:
                  return "up-bow";
                  break;
            case SymId::stringsDownBow:
                  return "down-bow";
                  break;
            case SymId::pluckedSnapPizzicatoAbove:
                  return "snap-pizzicato";
                  break;
            case SymId::brassMuteOpen:
                  return "open-string";
                  break;
            case SymId::stringsThumbPosition:
                  return "thumb-position";
                  break;
            default:
                  ; // nothing
                  break;
            }

      return "";
      }

//---------------------------------------------------------
//   writeChordLines
//---------------------------------------------------------

static void writeChordLines(const Chord* const chord, BrfWriter& brf, Notations& notations, Articulations& articulations)
      {
      for (Element* e : chord->el()) {
            qDebug("chordAttributes: el %p type %d (%s)", e, int(e->type()), e->name());
            if (e->type() == ElementType::CHORDLINE) {
                  ChordLine const* const cl = static_cast<ChordLine*>(e);
                  QString subtype;
                  switch (cl->chordLineType()) {
                        case ChordLineType::FALL:
                              subtype = "falloff";
                              break;
                        case ChordLineType::DOIT:
                              subtype = "doit";
                              break;
                        case ChordLineType::PLOP:
                              subtype = "plop";
                              break;
                        case ChordLineType::SCOOP:
                              subtype = "scoop";
                              break;
                        default:
                              qDebug("unknown ChordLine subtype %d", int(cl->chordLineType()));
                        }
                  if (subtype != "") {
                        notations.tag(brf);
                        articulations.tag(brf);
                        brf.tagE(subtype);
                        }
                  }
            }
      }

//---------------------------------------------------------
//   chordAttributes
//---------------------------------------------------------

void ExportBrf::chordAttributes(Chord* chord, Notations& notations, Technical& technical,
                                     TrillHash& trillStart, TrillHash& trillStop)
      {
      QVector<Element*> fl;
      for (Element* e : chord->segment()->annotations()) {
            if (e->track() == chord->track() && e->isFermata())
                  fl.push_back(e);
            }
      fermatas(fl, _brf, notations);

      const QVector<Articulation*> na = chord->articulations();
      // first the attributes whose elements are children of <articulations>
      Articulations articulations;
      for (const Articulation* a : na) {
            auto sid = a->symId();
            auto mxmlArtic = symIdToArtic(sid);

            if (mxmlArtic != "") {
                  if (sid == SymId::articMarcatoAbove || sid == SymId::articMarcatoBelow) {
                        if (a->up())
                              mxmlArtic += " type=\"up\"";
                        else
                              mxmlArtic += " type=\"down\"";
                        }

                  notations.tag(_brf);
                  articulations.tag(_brf);
                  _brf.tagE(mxmlArtic);
                  }
            }

      if (Breath* b = hasBreathMark(chord)) {
            notations.tag(_brf);
            articulations.tag(_brf);
            _brf.tagE(b->isCaesura() ? "caesura" : "breath-mark");
            }

      writeChordLines(chord, _brf, notations, articulations);

      articulations.etag(_brf);

      // then the attributes whose elements are children of <ornaments>
      Ornaments ornaments;
      for (const Articulation* a : na) {
            auto sid = a->symId();
            auto mxmlOrnam = symIdToOrnam(sid);

            if (mxmlOrnam != "") {
                  notations.tag(_brf);
                  ornaments.tag(_brf);
                  _brf.tagE(mxmlOrnam);
                  }
            }

      tremoloSingleStartStop(chord, notations, ornaments, _brf);
      wavyLineStartStop(chord, notations, ornaments, trillStart, trillStop);
      ornaments.etag(_brf);

      // and finally the attributes whose elements are children of <technical>
      for (const Articulation* a : na) {
            auto sid = a->symId();

            if (sid == SymId::stringsHarmonic) {
                  notations.tag(_brf);
                  technical.tag(_brf);
                  _brf.stag("harmonic");
                  _brf.tagE("natural");
                  _brf.etag();
                  }
            else {
                  auto mxmlTechn = symIdToTechn(sid);
                  if (mxmlTechn != "") {
                        notations.tag(_brf);
                        technical.tag(_brf);
                        _brf.tagE(mxmlTechn);
                        }
                  }
            }

      // check if all articulations were handled
      for (const Articulation* a : na) {
            auto sid = a->symId();
            if (symIdToArtic(sid) == ""
                && symIdToOrnam(sid) == ""
                && symIdToTechn(sid) == "") {
                  qDebug("unknown chord attribute %s", qPrintable(a->userName()));
                  }
            }
      }

//---------------------------------------------------------
//   arpeggiate
//---------------------------------------------------------

// <notations>
//   <arpeggiate direction="up"/>
//   </notations>

static void arpeggiate(Arpeggio* arp, bool front, bool back, BrfWriter& brf, Notations& notations)
      {
      QString tagName;
      switch (arp->arpeggioType()) {
            case ArpeggioType::NORMAL:
                  notations.tag(brf);
                  tagName = "arpeggiate";
                  break;
            case ArpeggioType::UP:          // fall through
            case ArpeggioType::UP_STRAIGHT: // not supported by MusicXML, export as normal arpeggio
                  notations.tag(brf);
                  tagName = "arpeggiate direction=\"up\"";
                  break;
            case ArpeggioType::DOWN:          // fall through
            case ArpeggioType::DOWN_STRAIGHT: // not supported by MusicXML, export as normal arpeggio
                  notations.tag(brf);
                  tagName = "arpeggiate direction=\"down\"";
                  break;
            case ArpeggioType::BRACKET:
                  if (front) {
                        notations.tag(brf);
                        tagName = "non-arpeggiate type=\"bottom\"";
                        }
                  if (back) {
                        notations.tag(brf);
                        tagName = "non-arpeggiate type=\"top\"";
                        }
                  break;
            default:
                  qDebug("unknown arpeggio subtype %d", int(arp->arpeggioType()));
                  break;
            }

      if (tagName != "") {
            tagName += addPositioningAttributes(arp);
            brf.tagE(tagName);
            }
      }

//---------------------------------------------------------
//   determineTupletNormalTicks
//---------------------------------------------------------

/**
 Determine the ticks in the normal type for the tuplet \a chord.
 This is non-zero only if chord if part of a tuplet containing
 different length duration elements.
 TODO determine how to handle baselen with dots and verify correct behaviour.
 TODO verify if baseLen should always be correctly set
      (it seems after MusicXMLimport this is not the case)
 */

static int determineTupletNormalTicks(ChordRest const* const chord)
      {
      Tuplet const* const t = chord->tuplet();
      if (!t)
            return 0;
      /*
      qDebug("determineTupletNormalTicks t %p baselen %s", t, qPrintable(t->baseLen().ticks().print()));
      for (int i = 0; i < t->elements().size(); ++i)
            qDebug("determineTupletNormalTicks t %p i %d ticks %s", t, i, qPrintable(t->elements().at(i)->ticks().print()));
            */
      for (unsigned int i = 1; i < t->elements().size(); ++i)
            if (t->elements().at(0)->ticks() != t->elements().at(i)->ticks())
                  return t->baseLen().ticks().ticks();
      if (t->elements().size() != (unsigned)(t->ratio().numerator()))
            return t->baseLen().ticks().ticks();
      return 0;
      }

//---------------------------------------------------------
//   beamFanAttribute
//---------------------------------------------------------

static QString beamFanAttribute(const Beam* const b)
      {
      const qreal epsilon = 0.1;

      QString fan;
      if ((b->growRight() - b->growLeft() > epsilon))
            fan = "accel";

      if ((b->growLeft() - b->growRight() > epsilon))
            fan = "rit";

      if (fan != "")
            return QString(" fan=\"%1\"").arg(fan);

      return "";
      }

//---------------------------------------------------------
//   writeBeam
//---------------------------------------------------------

//  beaming
//    <beam number="1">start</beam>
//    <beam number="1">end</beam>
//    <beam number="1">continue</beam>
//    <beam number="1">backward hook</beam>
//    <beam number="1">forward hook</beam>

static void writeBeam(BrfWriter& brf, ChordRest* const cr, Beam* const b)
      {
      const auto& elements = b->elements();
      const int idx = elements.indexOf(cr);
      if (idx == -1) {
            qDebug("Beam::writeBrf(): cannot find ChordRest");
            return;
            }
      int blp = -1; // beam level previous chord
      int blc = -1; // beam level current chord
      int bln = -1; // beam level next chord
      // find beam level previous chord
      for (int i = idx - 1; blp == -1 && i >= 0; --i) {
            const auto crst = elements[i];
            if (crst->isChord())
                  blp = toChord(crst)->beams();
            }
      // find beam level current chord
      if (cr->isChord())
            blc = toChord(cr)->beams();
      // find beam level next chord
      for (int i = idx + 1; bln == -1 && i < elements.size(); ++i) {
            const auto crst = elements[i];
            if (crst->isChord())
                  bln = toChord(crst)->beams();
            }
      // find beam type and write
      for (int i = 1; i <= blc; ++i) {
            QString text;
            if (blp < i && bln >= i) text = "begin";
            else if (blp < i && bln < i) {
                  if (bln > 0) text = "forward hook";
                  else if (blp > 0) text = "backward hook";
                  }
            else if (blp >= i && bln < i)
                  text = "end";
            else if (blp >= i && bln >= i)
                  text = "continue";
            if (text != "") {
                  QString tag = "beam";
                  tag += QString(" number=\"%1\"").arg(i);
                  if (text == "begin")
                        tag += beamFanAttribute(b);
                  brf.tag(tag, text);
                  }
            }
      }

//---------------------------------------------------------
//   instrId
//---------------------------------------------------------

static QString instrId(int partNr, int instrNr)
      {
      return QString("id=\"P%1-I%2\"").arg(partNr).arg(instrNr);
      }

//---------------------------------------------------------
//   writeNotehead
//---------------------------------------------------------

static void writeNotehead(BrfWriter& brf, const Note* const note)
      {
      QString noteheadTagname = QString("notehead");
      noteheadTagname += color2xml(note);
      bool leftParenthesis = false, rightParenthesis = false;
      for (Element* elem : note->el()) {
            if (elem->type() == ElementType::SYMBOL) {
                  Symbol* s = static_cast<Symbol*>(elem);
                  if (s->sym() == SymId::noteheadParenthesisLeft)
                        leftParenthesis = true;
                  else if (s->sym() == SymId::noteheadParenthesisRight)
                        rightParenthesis = true;
                  }
            }
      if (rightParenthesis && leftParenthesis)
            noteheadTagname += " parentheses=\"yes\"";
      if (note->headType() == NoteHead::Type::HEAD_QUARTER)
            noteheadTagname += " filled=\"yes\"";
      else if ((note->headType() == NoteHead::Type::HEAD_HALF) || (note->headType() == NoteHead::Type::HEAD_WHOLE))
            noteheadTagname += " filled=\"no\"";
      if (note->headGroup() == NoteHead::Group::HEAD_SLASH)
            brf.tag(noteheadTagname, "slash");
      else if (note->headGroup() == NoteHead::Group::HEAD_TRIANGLE_UP)
            brf.tag(noteheadTagname, "triangle");
      else if (note->headGroup() == NoteHead::Group::HEAD_DIAMOND)
            brf.tag(noteheadTagname, "diamond");
      else if (note->headGroup() == NoteHead::Group::HEAD_PLUS)
            brf.tag(noteheadTagname, "cross");
      else if (note->headGroup() == NoteHead::Group::HEAD_CROSS)
            brf.tag(noteheadTagname, "x");
      else if (note->headGroup() == NoteHead::Group::HEAD_XCIRCLE)
            brf.tag(noteheadTagname, "circle-x");
      else if (note->headGroup() == NoteHead::Group::HEAD_TRIANGLE_DOWN)
            brf.tag(noteheadTagname, "inverted triangle");
      else if (note->headGroup() == NoteHead::Group::HEAD_SLASHED1)
            brf.tag(noteheadTagname, "slashed");
      else if (note->headGroup() == NoteHead::Group::HEAD_SLASHED2)
            brf.tag(noteheadTagname, "back slashed");
      else if (note->headGroup() == NoteHead::Group::HEAD_DO)
            brf.tag(noteheadTagname, "do");
      else if (note->headGroup() == NoteHead::Group::HEAD_RE)
            brf.tag(noteheadTagname, "re");
      else if (note->headGroup() == NoteHead::Group::HEAD_MI)
            brf.tag(noteheadTagname, "mi");
      else if (note->headGroup() == NoteHead::Group::HEAD_FA && !note->chord()->up())
            brf.tag(noteheadTagname, "fa");
      else if (note->headGroup() == NoteHead::Group::HEAD_FA && note->chord()->up())
            brf.tag(noteheadTagname, "fa up");
      else if (note->headGroup() == NoteHead::Group::HEAD_LA)
            brf.tag(noteheadTagname, "la");
      else if (note->headGroup() == NoteHead::Group::HEAD_TI)
            brf.tag(noteheadTagname, "ti");
      else if (note->headGroup() == NoteHead::Group::HEAD_SOL)
            brf.tag(noteheadTagname, "so");
      else if (note->color() != MScore::defaultColor)
            brf.tag(noteheadTagname, "normal");
      else if (rightParenthesis && leftParenthesis)
            brf.tag(noteheadTagname, "normal");
      else if (note->headType() != NoteHead::Type::HEAD_AUTO)
            brf.tag(noteheadTagname, "normal");
      }

//---------------------------------------------------------
//   writeFingering
//---------------------------------------------------------

static void writeFingering(BrfWriter& brf, Notations& notations, Technical& technical, const Note* const note)
      {
      for (const Element* e : note->el()) {
            if (e->type() == ElementType::FINGERING) {
                  const TextBase* f = toTextBase(e);
                  notations.tag(brf);
                  technical.tag(brf);
                  QString t = MScoreTextToBraille::toPlainText(f->xmlText());
                  if (f->tid() == Tid::RH_GUITAR_FINGERING)
                        brf.tag("pluck", t);
                  else if (f->tid() == Tid::LH_GUITAR_FINGERING)
                        brf.tag("fingering", t);
                  else if (f->tid() == Tid::FINGERING) {
                        // for generic fingering, try to detect plucking
                        // (backwards compatibility with MuseScore 1.x)
                        // p, i, m, a, c represent the plucking finger
                        if (t == "p" || t == "i" || t == "m" || t == "a" || t == "c")
                              brf.tag("pluck", t);
                        else
                              brf.tag("fingering", t);
                        }
                  else if (f->tid() == Tid::STRING_NUMBER) {
                        bool ok;
                        int i = t.toInt(&ok);
                        if (ok) {
                              if (i == 0)
                                    brf.tagE("open-string");
                              else if (i > 0)
                                    brf.tag("string", t);
                              }
                        if (!ok || i < 0)
                              qDebug("invalid string number '%s'", qPrintable(t));
                        }
                  else
                        qDebug("unknown fingering style");
                  }
            else {
                  // TODO
                  }
            }
      }

//---------------------------------------------------------
//   stretchCorrActTicks
//---------------------------------------------------------

static int stretchCorrActTicks(const Note* const note)
      {
      // time signature stretch factor
      const Fraction str = note->chord()->staff()->timeStretch(note->chord()->tick());
      // chord's actual ticks corrected for stretch
      return (note->chord()->actualTicks() * str).ticks();
      }

//---------------------------------------------------------
//   tremoloCorrection
//---------------------------------------------------------

// duration correction for two note tremolo
static int tremoloCorrection(const Note* const note)
      {
      int tremCorr = 1;
      if (isTwoNoteTremolo(note->chord())) tremCorr = 2;
      return tremCorr;
      }

//---------------------------------------------------------
//   writeTypeAndDots
//---------------------------------------------------------

static void writeTypeAndDots(BrfWriter& brf, const Note* const note)
      {
      // type
      int dots = 0;
      Tuplet* t = note->chord()->tuplet();
      int actNotes = 1;
      int nrmNotes = 1;
      if (t) {
            actNotes = t->ratio().numerator();
            nrmNotes = t->ratio().denominator();
            }

      const auto strActTicks = stretchCorrActTicks(note);
      Fraction tt = Fraction::fromTicks(strActTicks * actNotes * tremoloCorrection(note) / nrmNotes);
      QString s = tick2xml(tt, &dots);
      if (s.isEmpty())
            qDebug("no note type found for ticks %d", strActTicks);

      if (note->small())
            brf.tag("type size=\"cue\"", s);
      else
            brf.tag("type", s);
      for (int ni = dots; ni > 0; ni--)
            brf.tagE("dot");
      }

//---------------------------------------------------------
//   writeTimeModification
//---------------------------------------------------------

static void writeTimeModification(BrfWriter& brf, const Note* const note)
      {
      // time modification for two note tremolo
      // TODO: support tremolo in tuplet ?
      if (tremoloCorrection(note) == 2) {
            brf.stag("time-modification");
            brf.tag("actual-notes", 2);
            brf.tag("normal-notes", 1);
            brf.etag();
            }

      // time modification for tuplet
      const auto t = note->chord()->tuplet();
      if (t) {
            auto actNotes = t->ratio().numerator();
            auto nrmNotes = t->ratio().denominator();
            auto nrmTicks = determineTupletNormalTicks(note->chord());
            // TODO: remove following duplicated code (present for both notes and rests)
            brf.stag("time-modification");
            brf.tag("actual-notes", actNotes);
            brf.tag("normal-notes", nrmNotes);
            //qDebug("nrmTicks %d", nrmTicks);
            if (nrmTicks > 0) {
                  int nrmDots = 0;
                  QString nrmType = tick2xml(Fraction::fromTicks(nrmTicks), &nrmDots);
                  if (nrmType.isEmpty())
                        qDebug("no note type found for ticks %d", nrmTicks);
                  else {
                        brf.tag("normal-type", nrmType);
                        for (int ni = nrmDots; ni > 0; ni--)
                              brf.tagE("normal-dot");
                        }
                  }
            brf.etag();
            }
      }

//---------------------------------------------------------
//   writePitch
//---------------------------------------------------------

static void writePitch(BrfWriter& brf, const Note* const note, const bool useDrumset)
      {
      // step / alter / octave
      QString step;
      int alter = 0;
      int octave = 0;
      const auto chord = note->chord();
      if (chord->staff() && chord->staff()->isTabStaff(Fraction(0,1))) {
            tabpitch2xml(note->pitch(), note->tpc(), step, alter, octave);
            }
      else {
            if (!useDrumset) {
                  pitch2xml(note, step, alter, octave);
                  }
            else {
                  unpitch2xml(note, step, octave);
                  }
            }
      brf.stag(useDrumset ? "unpitched" : "pitch");
      brf.tag(useDrumset  ? "display-step" : "step", step);
      // Check for microtonal accidentals and overwrite "alter" tag
      auto acc = note->accidental();
      double alter2 = 0.0;
      if (acc) {
            switch (acc->accidentalType()) {
                  case AccidentalType::MIRRORED_FLAT:  alter2 = -0.5; break;
                  case AccidentalType::SHARP_SLASH:    alter2 = 0.5;  break;
                  case AccidentalType::MIRRORED_FLAT2: alter2 = -1.5; break;
                  case AccidentalType::SHARP_SLASH4:   alter2 = 1.5;  break;
                  default:                                            break;
                  }
            }
      if (alter && !alter2)
            brf.tag("alter", alter);
      if (!alter && alter2)
            brf.tag("alter", alter2);
      // TODO what if both alter and alter2 are present? For Example: playing with transposing instruments
      brf.tag(useDrumset ? "display-octave" : "octave", octave);
      brf.etag();
      }

//---------------------------------------------------------
//   notePosition
//---------------------------------------------------------

static QString notePosition(const ExportBrf* const expMbrf, const Note* const note)
      {
      QString res;

      if (preferences.getBool(PREF_EXPORT_BRAILLE_EXPORTLAYOUT)) {
            const double pageHeight  = expMbrf->getTenthsFromInches(expMbrf->score()->styleD(Sid::pageHeight));

            const auto chord = note->chord();

            double measureX = expMbrf->getTenthsFromDots(chord->measure()->pagePos().x());
            double measureY = pageHeight - expMbrf->getTenthsFromDots(chord->measure()->pagePos().y());
            double noteX = expMbrf->getTenthsFromDots(note->pagePos().x());
            double noteY = pageHeight - expMbrf->getTenthsFromDots(note->pagePos().y());

            res += QString(" default-x=\"%1\"").arg(QString::number(noteX - measureX,'f',2));
            res += QString(" default-y=\"%1\"").arg(QString::number(noteY - measureY,'f',2));
            }

      return res;
      }

//---------------------------------------------------------
//   chord
//---------------------------------------------------------

/**
 Write \a chord on \a staff with lyriclist \a ll.

 For a single-staff part, \a staff equals zero, suppressing the <staff> element.
 */

void ExportBrf::chord(Chord* chord, int staff, const std::vector<Lyrics*>* ll, bool useDrumset)
      {
      Part* part = chord->score()->staff(chord->track() / VOICES)->part();
      int partNr = _score->parts().indexOf(part);
      int instNr = instrMap.value(part->instrument(_tick), -1);
      /*
      qDebug("chord() %p parent %p isgrace %d #gracenotes %d graceidx %d",
             chord, chord->parent(), chord->isGrace(), chord->graceNotes().size(), chord->graceIndex());
      qDebug("track %d tick %d part %p nr %d instr %p nr %d",
             chord->track(), chord->tick(), part, partNr, part->instrument(tick), instNr);
      for (Element* e : chord->el())
            qDebug("chord %p el %p", chord, e);
       */
      std::vector<Note*> nl = chord->notes();
      bool grace = chord->isGrace();
      if (!grace) _tick += chord->actualTicks();
#ifdef DEBUG_TICK
      qDebug("ExportBrf::chord() oldtick=%d", tick);
      qDebug("notetype=%d grace=%d", gracen, grace);
      qDebug(" newtick=%d", tick);
#endif

      for (Note* note : nl) {
            QString val;

            _attr.doAttr(_brf, false);
            QString noteTag = QString("note");

            noteTag += notePosition(this, note);

            if (!note->visible()) {
                  noteTag += QString(" print-object=\"no\"");
                  }
            //TODO support for OFFSET_VAL
            if (note->veloType() == Note::ValueType::USER_VAL) {
                  int velo = note->veloOffset();
                  noteTag += QString(" dynamics=\"%1\"").arg(QString::number(velo * 100.0 / 90.0,'f',2));
                  }
            _brf.stag(noteTag);

            if (grace) {
                  if (note->noteType() == NoteType::ACCIACCATURA)
                        _brf.tagE("grace slash=\"yes\"");
                  else
                        _brf.tagE("grace");
                  }
            if (note != nl.front())
                  _brf.tagE("chord");
            else if (note->chord()->small()) // need this only once per chord
                  _brf.tagE("cue");

            writePitch(_brf, note, useDrumset);

            // duration
            if (!grace)
                  _brf.tag("duration", stretchCorrActTicks(note) / div);

            if (note->tieBack())
                  _brf.tagE("tie type=\"stop\"");
            if (note->tieFor())
                  _brf.tagE("tie type=\"start\"");

            // instrument for multi-instrument or unpitched parts
            if (!useDrumset) {
                  if (instrMap.size() > 1 && instNr >= 0)
                        _brf.tagE(QString("instrument %1").arg(instrId(partNr + 1, instNr + 1)));
                  }
            else
                  _brf.tagE(QString("instrument %1").arg(instrId(partNr + 1, note->pitch() + 1)));

            // voice
            // for a single-staff part, staff is 0, which needs to be corrected
            // to calculate the correct voice number
            int voice = (staff-1) * VOICES + note->chord()->voice() + 1;
            if (staff == 0)
                  voice += VOICES;

            _brf.tag("voice", voice);

            writeTypeAndDots(_brf, note);
            writeAccidental(_brf, "accidental", note->accidental());
            writeTimeModification(_brf, note);

            // no stem for whole notes and beyond
            if (chord->noStem() || chord->measure()->stemless(chord->staffIdx())) {
                  _brf.tag("stem", QString("none"));
                  }
            else if (note->chord()->stem()) {
                  _brf.tag("stem", QString(note->chord()->up() ? "up" : "down"));
                  }

            writeNotehead(_brf, note);

            // LVIFIX: check move() handling
            if (staff)
                  _brf.tag("staff", staff + note->chord()->staffMove());

            if (note == nl.front() && chord->beam())
                  writeBeam(_brf, chord, chord->beam());

            Notations notations;
            Technical technical;

            const Tie* tieBack = note->tieBack();
            if (tieBack) {
                  notations.tag(_brf);
                  _brf.tagE("tied type=\"stop\"");
                  }
            const Tie* tieFor = note->tieFor();
            if (tieFor) {
                  notations.tag(_brf);
                  QString rest = slurTieLineStyle(tieFor);
                  _brf.tagE(QString("tied type=\"start\"%1").arg(rest));
                  }

            if (note == nl.front()) {
                  if (!grace)
                        tupletStartStop(chord, notations, _brf);

                  sh.doSlurs(chord, notations, _brf);

                  chordAttributes(chord, notations, technical, _trillStart, _trillStop);
                  }

            writeFingering(_brf, notations, technical, note);

            // write tablature string / fret
            if (chord->staff() && chord->staff()->isTabStaff(Fraction(0,1)))
                  if (note->fret() >= 0 && note->string() >= 0) {
                        notations.tag(_brf);
                        technical.tag(_brf);
                        _brf.tag("string", note->string() + 1);
                        _brf.tag("fret", note->fret());
                        }

            technical.etag(_brf);
            if (chord->arpeggio()) {
                  arpeggiate(chord->arpeggio(), note == nl.front(), note == nl.back(), _brf, notations);
                  }
            for (Spanner* spanner : note->spannerFor())
                  if (spanner->type() == ElementType::GLISSANDO) {
                        gh.doGlissandoStart(static_cast<Glissando*>(spanner), notations, _brf);
                        }
            for (Spanner* spanner : note->spannerBack())
                  if (spanner->type() == ElementType::GLISSANDO) {
                        gh.doGlissandoStop(static_cast<Glissando*>(spanner), notations, _brf);
                        }
            // write glissando (only for last note)
            /*
            Chord* ch = nextChord(chord);
            if ((note == nl.back()) && ch && ch->glissando()) {
                  gh.doGlissandoStart(ch, notations, brf);
                  }
            if (chord->glissando()) {
                  gh.doGlissandoStop(chord, notations, brf);
                  }
            */
            notations.etag(_brf);
            // write lyrics (only for first note)
            if (!grace && (note == nl.front()) && ll)
                  lyrics(ll, chord->track());
            _brf.etag();
            }
      }

//---------------------------------------------------------
//   rest
//---------------------------------------------------------

/**
 Write \a rest on \a staff.

 For a single-staff part, \a staff equals zero, suppressing the <staff> element.
 */

void ExportBrf::rest(Rest* rest, int staff)
      {
      static char table2[]  = "CDEFGAB";
#ifdef DEBUG_TICK
      qDebug("ExportBrf::rest() oldtick=%d", tick);
#endif
      _attr.doAttr(_brf, false);

      QString noteTag = QString("note");
      noteTag += color2xml(rest);
      if (!rest->visible() ) {
            noteTag += QString(" print-object=\"no\"");
            }
      _brf.stag(noteTag);

      int yOffsSt   = 0;
      int oct       = 0;
      int stp       = 0;
      ClefType clef = rest->staff()->clef(rest->tick());
      int po        = ClefInfo::pitchOffset(clef);

      // Determine y position, but leave at zero in case of tablature staff
      // as no display-step or display-octave should be written for a tablature staff,

      if (clef != ClefType::TAB && clef != ClefType::TAB_SERIF && clef != ClefType::TAB4 && clef != ClefType::TAB4_SERIF) {
            double yOffsSp = rest->offset().y() / rest->spatium();              // y offset in spatium (negative = up)
            yOffsSt = -2 * int(yOffsSp > 0.0 ? yOffsSp + 0.5 : yOffsSp - 0.5); // same rounded to int (positive = up)

            po -= 4;    // pitch middle staff line (two lines times two steps lower than top line)
            po += yOffsSt; // rest "pitch"
            oct = po / 7; // octave
            stp = po % 7; // step
            }

      // Either <rest/>
      // or <rest><display-step>F</display-step><display-octave>5</display-octave></rest>
      if (yOffsSt == 0) {
            _brf.tagE("rest");
            }
      else {
            _brf.stag("rest");
            _brf.tag("display-step", QString(QChar(table2[stp])));
            _brf.tag("display-octave", oct - 1);
            _brf.etag();
            }

      TDuration d = rest->durationType();
      Fraction tickLen = rest->actualTicks();
      if (d.type() == TDuration::DurationType::V_MEASURE) {
            // to avoid forward since rest->ticklen=0 in this case.
            tickLen = rest->measure()->ticks();
            }
      _tick += tickLen;
#ifdef DEBUG_TICK
      qDebug(" tickLen=%d newtick=%d", tickLen, tick);
#endif

      _brf.tag("duration", tickLen.ticks() / div);

      // for a single-staff part, staff is 0, which needs to be corrected
      // to calculate the correct voice number
      int voice = (staff-1) * VOICES + rest->voice() + 1;
      if (staff == 0)
            voice += VOICES;
      _brf.tag("voice", voice);

      // do not output a "type" element for whole measure rest
      if (d.type() != TDuration::DurationType::V_MEASURE) {
            QString s = d.name();
            int dots  = rest->dots();
            if (rest->small())
                  _brf.tag("type size=\"cue\"", s);
            else
                  _brf.tag("type", s);
            for (int i = dots; i > 0; i--)
                  _brf.tagE("dot");
            }

      if (rest->tuplet()) {
            Tuplet* t = rest->tuplet();
            _brf.stag("time-modification");
            _brf.tag("actual-notes", t->ratio().numerator());
            _brf.tag("normal-notes", t->ratio().denominator());
            int nrmTicks = determineTupletNormalTicks(rest);
            if (nrmTicks > 0) {
                  int nrmDots = 0;
                  QString nrmType = tick2xml(Fraction::fromTicks(nrmTicks), &nrmDots);
                  if (nrmType.isEmpty())
                        qDebug("no note type found for ticks %d", nrmTicks);
                  else {
                        _brf.tag("normal-type", nrmType);
                        for (int ni = nrmDots; ni > 0; ni--)
                              _brf.tagE("normal-dot");
                        }
                  }
            _brf.etag();
            }

      if (staff)
            _brf.tag("staff", staff);

      Notations notations;
      QVector<Element*> fl;
      for (Element* e : rest->segment()->annotations()) {
            if (e->isFermata() && e->track() == rest->track())
                  fl.push_back(e);
            }
      fermatas(fl, _brf, notations);

      sh.doSlurs(rest, notations, _brf);

      tupletStartStop(rest, notations, _brf);
      notations.etag(_brf);

      _brf.etag();
      }

//---------------------------------------------------------
//   directionTag
//---------------------------------------------------------

static void directionTag(BrfWriter& brf, Attributes& attr, Element const* const el = 0)
      {
      attr.doAttr(brf, false);
      QString tagname = QString("direction");
      if (el) {
            /*
             qDebug("directionTag() spatium=%g elem=%p tp=%d (%s)\ndirectionTag()  x=%g y=%g xsp,ysp=%g,%g w=%g h=%g userOff.y=%g",
                    el->spatium(),
                    el,
                    el->type(),
                    el->name(),
                    el->x(), el->y(),
                    el->x()/el->spatium(), el->y()/el->spatium(),
                    el->width(), el->height(),
                    el->offset().y()
                   );
             */
            const Element* pel = 0;
            const LineSegment* seg = 0;
            if (el->type() == ElementType::HAIRPIN || el->type() == ElementType::OTTAVA
                || el->type() == ElementType::PEDAL || el->type() == ElementType::TEXTLINE) {
                  // handle elements derived from SLine
                  // find the system containing the first linesegment
                  const SLine* sl = static_cast<const SLine*>(el);
                  if (!sl->segmentsEmpty()) {
                        seg = toLineSegment(sl->frontSegment());
                        /*
                         qDebug("directionTag()  seg=%p x=%g y=%g w=%g h=%g cpx=%g cpy=%g userOff.y=%g",
                                seg, seg->x(), seg->y(),
                                seg->width(), seg->height(),
                                seg->pagePos().x(), seg->pagePos().y(),
                                seg->offset().y());
                         */
                        pel = seg->parent();
                        }
                  }
            else if (el->type() == ElementType::DYNAMIC
                     || el->type() == ElementType::INSTRUMENT_CHANGE
                     || el->type() == ElementType::REHEARSAL_MARK
                     || el->type() == ElementType::STAFF_TEXT
                     || el->type() == ElementType::SYMBOL
                     || el->type() == ElementType::TEXT) {
                  // handle other elements attached (e.g. via Segment / Measure) to a system
                  // find the system containing this element
                  for (const Element* e = el; e; e = e->parent()) {
                        if (e->type() == ElementType::SYSTEM) pel = e;
                        }
                  }
            else
                  qDebug("directionTag() element %p tp=%d (%s) not supported",
                         el, int(el->type()), el->name());

            /*
             if (pel) {
             qDebug("directionTag()  prnt tp=%d (%s) x=%g y=%g w=%g h=%g userOff.y=%g",
                    pel->type(),
                    pel->name(),
                    pel->x(), pel->y(),
                    pel->width(), pel->height(),
                    pel->offset().y());
                  }
             */

            if (pel && pel->type() == ElementType::SYSTEM) {
                  /*
                  const System* sys = static_cast<const System*>(pel);
                  QRectF bb = sys->staff(el->staffIdx())->bbox();
                  qDebug("directionTag()  syst=%p sys x=%g y=%g cpx=%g cpy=%g",
                         sys, sys->pos().x(),  sys->pos().y(),
                         sys->pagePos().x(),
                         sys->pagePos().y()
                        );
                  qDebug("directionTag()  staf x=%g y=%g w=%g h=%g",
                         bb.x(), bb.y(),
                         bb.width(), bb.height());
                  // element is above the staff if center of bbox is above center of staff
                  qDebug("directionTag()  center diff=%g", el->y() + el->height() / 2 - bb.y() - bb.height() / 2);
                   */

                  if (el->isHairpin() || el->isOttava() || el->isPedal() || el->isTextLine()) {
                        // for the line type elements the reference point is vertically centered
                        // actual position info is in the segments
                        // compare the segment's canvas ypos with the staff's center height
                        // if (seg->pagePos().y() < sys->pagePos().y() + bb.y() + bb.height() / 2)
                        if (el->placement() == Placement::ABOVE)
                              tagname += " placement=\"above\"";
                        else
                              tagname += " placement=\"below\"";
                        }
                  else if (el->isDynamic()) {
                        tagname += " placement=\"";
                        tagname += el->placement() == Placement::ABOVE ? "above" : "below";
                        tagname += "\"";
                        }
                  else {
                        /*
                        qDebug("directionTag()  staf ely=%g elh=%g bby=%g bbh=%g",
                               el->y(), el->height(),
                               bb.y(), bb.height());
                         */
                        // if (el->y() + el->height() / 2 < /*bb.y() +*/ bb.height() / 2)
                        if (el->placement() == Placement::ABOVE)
                              tagname += " placement=\"above\"";
                        else
                              tagname += " placement=\"below\"";
                        }
                  } // if (pel && ...
            }
      brf.stag(tagname);
      }

//---------------------------------------------------------
//   directionETag
//---------------------------------------------------------

static void directionETag(BrfWriter& brf, int staff, int offs = 0)
      {
      if (offs)
            brf.tag("offset", offs);
      if (staff)
            brf.tag("staff", staff);
      brf.etag();
      }

//---------------------------------------------------------
//   partGroupStart
//---------------------------------------------------------

static void partGroupStart(BrfWriter& brf, int number, BracketType bracket)
      {
      brf.stag(QString("part-group type=\"start\" number=\"%1\"").arg(number));
      QString br = "";
      switch (bracket) {
            case BracketType::NO_BRACKET:
                  br = "none";
                  break;
            case BracketType::NORMAL:
                  br = "bracket";
                  break;
            case BracketType::BRACE:
                  br = "brace";
                  break;
            case BracketType::LINE:
                  br = "line";
                  break;
            case BracketType::SQUARE:
                  br = "square";
                  break;
            default:
                  qDebug("bracket subtype %d not understood", int(bracket));
            }
      if (br != "")
            brf.tag("group-symbol", br);
      brf.etag();
      }

//---------------------------------------------------------
//   words
//---------------------------------------------------------

static bool findUnit(TDuration::DurationType val, QString& unit)
      {
      unit = "";
      switch (val) {
            case TDuration::DurationType::V_HALF: unit = "half"; break;
            case TDuration::DurationType::V_QUARTER: unit = "quarter"; break;
            case TDuration::DurationType::V_EIGHTH: unit = "eighth"; break;
            default: qDebug("findUnit: unknown DurationType %d", int(val));
            }
      return true;
      }

static bool findMetronome(const QList<TextFragment>& list,
                          QList<TextFragment>& wordsLeft,  // words left of metronome
                          bool& hasParen,      // parenthesis
                          QString& metroLeft,  // left part of metronome
                          QString& metroRight, // right part of metronome
                          QList<TextFragment>& wordsRight // words right of metronome
                          )
      {
      QString words = MScoreTextToBraille::toPlainTextPlusSymbols(list);
      //qDebug("findMetronome('%s')", qPrintable(words));
      hasParen   = false;
      metroLeft  = "";
      metroRight = "";
      int metroPos = -1;   // metronome start position
      int metroLen = 0;    // metronome length

      int indEq  = words.indexOf('=');
      if (indEq <= 0)
            return false;

      int len1 = 0;
      TDuration dur;

      // find first note, limiting search to the part left of the first '=',
      // to prevent matching the second note in a "note1 = note2" metronome
      int pos1 = TempoText::findTempoDuration(words.left(indEq), len1, dur);
      QRegExp eq("\\s*=\\s*");
      int pos2 = eq.indexIn(words, pos1 + len1);
      if (pos1 != -1 && pos2 == pos1 + len1) {
            int len2 = eq.matchedLength();
            if (words.length() > pos2 + len2) {
                  QString s1 = words.mid(0, pos1);     // string to the left of metronome
                  QString s2 = words.mid(pos1, len1);  // first note
                  QString s3 = words.mid(pos2, len2);  // equals sign
                  QString s4 = words.mid(pos2 + len2); // string to the right of equals sign
                  /*
                  qDebug("found note and equals: '%s'%s'%s'%s'",
                         qPrintable(s1),
                         qPrintable(s2),
                         qPrintable(s3),
                         qPrintable(s4)
                         );
                   */

                  // now determine what is to the right of the equals sign
                  // must have either a (dotted) note or a number at start of s4
                  int len3 = 0;
                  QRegExp nmb("\\d+");
                  int pos3 = TempoText::findTempoDuration(s4, len3, dur);
                  if (pos3 == -1) {
                        // did not find note, try to find a number
                        pos3 = nmb.indexIn(s4);
                        if (pos3 == 0)
                              len3 = nmb.matchedLength();
                        }
                  if (pos3 == -1)
                        // neither found
                        return false;

                  QString s5 = s4.mid(0, len3); // number or second note
                  QString s6 = s4.mid(len3);    // string to the right of metronome
                  /*
                  qDebug("found right part: '%s'%s'",
                         qPrintable(s5),
                         qPrintable(s6)
                         );
                   */

                  // determine if metronome has parentheses
                  // left part of string must end with parenthesis plus optional spaces
                  // right part of string must have parenthesis (but not in first pos)
                  int lparen = s1.indexOf("(");
                  int rparen = s6.indexOf(")");
                  hasParen = (lparen == s1.length() - 1 && rparen == 0);

                  metroLeft = s2;
                  metroRight = s5;

                  metroPos = pos1;               // metronome position
                  metroLen = len1 + len2 + len3; // metronome length
                  if (hasParen) {
                        metroPos -= 1;           // move left one position
                        metroLen += 2;           // add length of '(' and ')'
                        }

                  // calculate starting position corrected for surrogate pairs
                  // (which were ignored by toPlainTextPlusSymbols())
                  int corrPos = metroPos;
                  for (int i = 0; i < metroPos; ++i)
                        if (words.at(i).isHighSurrogate())
                              --corrPos;
                  metroPos = corrPos;

                  /*
                  qDebug("-> found '%s'%s' hasParen %d metro pos %d len %d",
                         qPrintable(metroLeft),
                         qPrintable(metroRight),
                         hasParen, metroPos, metroLen
                         );
                   */
                  QList<TextFragment> mid; // not used
                  MScoreTextToBraille::split(list, metroPos, metroLen, wordsLeft, mid, wordsRight);
                  return true;
                  }
            }
      return false;
      }

static void beatUnit(BrfWriter& brf, const TDuration dur)
      {
      int dots = dur.dots();
      QString unit;
      findUnit(dur.type(), unit);
      brf.tag("beat-unit", unit);
      while (dots > 0) {
            brf.tagE("beat-unit-dot");
            --dots;
            }
      }

static void wordsMetrome(BrfWriter& brf, Score* s, TextBase const* const text)
      {
      //qDebug("wordsMetrome('%s')", qPrintable(text->xmlText()));
      const QList<TextFragment> list = text->fragmentList();
      QList<TextFragment>       wordsLeft;  // words left of metronome
      bool hasParen;                        // parenthesis
      QString metroLeft;                    // left part of metronome
      QString metroRight;                   // right part of metronome
      QList<TextFragment>       wordsRight; // words right of metronome

      // set the default words format
      const QString mtf = s->styleSt(Sid::MusicalTextFont);
      CharFormat defFmt;
      defFmt.setFontFamily(s->styleSt(Sid::staffTextFontFace));
      defFmt.setFontSize(s->styleD(Sid::staffTextFontSize));

      if (findMetronome(list, wordsLeft, hasParen, metroLeft, metroRight, wordsRight)) {
            if (wordsLeft.size() > 0) {
                  brf.stag("direction-type");
                  QString attr; // TODO TBD
                  attr += addPositioningAttributes(text);
                  MScoreTextToBraille mttm("words", attr, defFmt, mtf);
                  mttm.writeTextFragments(wordsLeft, brf);
                  brf.etag();
                  }

            brf.stag("direction-type");
            QString tagName = QString("metronome parentheses=\"%1\"").arg(hasParen ? "yes" : "no");
            tagName += addPositioningAttributes(text);
            brf.stag(tagName);
            int len1 = 0;
            TDuration dur;
            TempoText::findTempoDuration(metroLeft, len1, dur);
            beatUnit(brf, dur);

            if (TempoText::findTempoDuration(metroRight, len1, dur) != -1)
                  beatUnit(brf, dur);
            else
                  brf.tag("per-minute", metroRight);

            brf.etag();
            brf.etag();

            if (wordsRight.size() > 0) {
                  brf.stag("direction-type");
                  QString attr; // TODO TBD
                  attr += addPositioningAttributes(text);
                  MScoreTextToBraille mttm("words", attr, defFmt, mtf);
                  mttm.writeTextFragments(wordsRight, brf);
                  brf.etag();
                  }
            }

      else {
            brf.stag("direction-type");
            QString attr;
            if (text->hasFrame()) {
                  if (text->circle())
                        attr = " enclosure=\"circle\"";
                  else
                        attr = " enclosure=\"rectangle\"";
                  }
            attr += addPositioningAttributes(text);
            MScoreTextToBraille mttm("words", attr, defFmt, mtf);
            //qDebug("words('%s')", qPrintable(text->text()));
            mttm.writeTextFragments(text->fragmentList(), brf);
            brf.etag();
            }
      }

void ExportBrf::tempoText(TempoText const* const text, int staff)
      {
      /*
      qDebug("ExportBrf::tempoText(TempoText='%s')", qPrintable(text->xmlText()));
      */
      _attr.doAttr(_brf, false);
      _brf.stag(QString("direction placement=\"%1\"").arg((text->placement() ==Placement::BELOW ) ? "below" : "above"));
      wordsMetrome(_brf, _score, text);
      /*
      int offs = text->mxmlOff();
      if (offs)
            brf.tag("offset", offs);
      */
      if (staff)
            _brf.tag("staff", staff);
      _brf.tagE(QString("sound tempo=\"%1\"").arg(QString::number(text->tempo()*60.0)));
      _brf.etag();
      }

//---------------------------------------------------------
//   words
//---------------------------------------------------------

void ExportBrf::words(TextBase const* const text, int staff)
      {
      /*
      qDebug("ExportBrf::words userOff.x=%f userOff.y=%f xmlText='%s' plainText='%s'",
             text->offset().x(), text->offset().y(),
             qPrintable(text->xmlText()),
             qPrintable(text->plainText()));
      */

      if (text->plainText() == "") {
            // sometimes empty Texts are present, exporting would result
            // in invalid MusicXML (as an empty direction-type would be created)
            return;
            }

      directionTag(_brf, _attr, text);
      wordsMetrome(_brf, _score, text);
      directionETag(_brf, staff);
      }

//---------------------------------------------------------
//   rehearsal
//---------------------------------------------------------

void ExportBrf::rehearsal(RehearsalMark const* const rmk, int staff)
      {
      if (rmk->plainText() == "") {
            // sometimes empty Texts are present, exporting would result
            // in invalid MusicXML (as an empty direction-type would be created)
            return;
            }

      directionTag(_brf, _attr, rmk);
      _brf.stag("direction-type");
      QString attr;
      attr += addPositioningAttributes(rmk);
      if (!rmk->hasFrame()) attr = " enclosure=\"none\"";
      // set the default words format
      const QString mtf = _score->styleSt(Sid::MusicalTextFont);
      CharFormat defFmt;
      defFmt.setFontFamily(_score->styleSt(Sid::staffTextFontFace));
      defFmt.setFontSize(_score->styleD(Sid::staffTextFontSize));
      // write formatted
      MScoreTextToBraille mttm("rehearsal", attr, defFmt, mtf);
      mttm.writeTextFragments(rmk->fragmentList(), _brf);
      _brf.etag();
      directionETag(_brf, staff);
      }

//---------------------------------------------------------
//   findDashes -- get index of hairpin in dashes table
//   return -1 if not found
//---------------------------------------------------------

int ExportBrf::findDashes(const TextLineBase* hp) const
      {
      for (int i = 0; i < MAX_NUMBER_LEVEL; ++i)
            if (dashes[i] == hp) return i;
      return -1;
      }

//---------------------------------------------------------
//   findHairpin -- get index of hairpin in hairpin table
//   return -1 if not found
//---------------------------------------------------------

int ExportBrf::findHairpin(const Hairpin* hp) const
      {
      for (int i = 0; i < MAX_NUMBER_LEVEL; ++i)
            if (hairpins[i] == hp) return i;
      return -1;
      }

//---------------------------------------------------------
//   fontSyleToBRF
//---------------------------------------------------------

static QString fontSyleToBRF(const FontStyle style)
      {
      QString res;
      if (style & FontStyle::Bold)
            res += " font-weight=\"bold\"";
      else if (style & FontStyle::Italic)
            res += " font-style=\"italic\"";
      else if (style & FontStyle::Underline)
            res += " underline=\"1\"";
      return res;
      }

//---------------------------------------------------------
//   hairpin
//---------------------------------------------------------

void ExportBrf::hairpin(Hairpin const* const hp, int staff, const Fraction& tick)
      {
      const auto isLineType = hp->isLineType();
      int n;
      if (isLineType) {
            n = findDashes(hp);
            if (n >= 0)
                  dashes[n] = nullptr;
            else {
                  n = findDashes(nullptr);
                  if (n >= 0)
                        dashes[n] = hp;
                  else {
                        qDebug("too many overlapping dashes (hp %p staff %d tick %d)", hp, staff, tick.ticks());
                        return;
                        }
                  }
            }
      else {
            n = findHairpin(hp);
            if (n >= 0)
                  hairpins[n] = nullptr;
            else {
                  n = findHairpin(nullptr);
                  if (n >= 0)
                        hairpins[n] = hp;
                  else {
                        qDebug("too many overlapping hairpins (hp %p staff %d tick %d)", hp, staff, tick.ticks());
                        return;
                        }
                  }
            }

      directionTag(_brf, _attr, hp);
      if (isLineType) {
            if (hp->tick() == tick) {
                  _brf.stag("direction-type");
                  QString tag = "words";
                  tag += QString(" font-family=\"%1\"").arg(hp->getProperty(Pid::BEGIN_FONT_FACE).toString());
                  tag += QString(" font-size=\"%1\"").arg(hp->getProperty(Pid::BEGIN_FONT_SIZE).toReal());
                  tag += fontSyleToBRF(static_cast<FontStyle>(hp->getProperty(Pid::BEGIN_FONT_STYLE).toInt()));
                  tag += addPositioningAttributes(hp, hp->tick() == tick);
                  _brf.tag(tag, hp->getProperty(Pid::BEGIN_TEXT));
                  _brf.etag();

                  _brf.stag("direction-type");
                  tag = "dashes type=\"start\"";
                  tag += QString(" number=\"%1\"").arg(n + 1);
                  tag += addPositioningAttributes(hp, hp->tick() == tick);
                  _brf.tagE(tag);
                  _brf.etag();
                  }
            else {
                  _brf.stag("direction-type");
                  _brf.tagE(QString("dashes type=\"stop\" number=\"%1\"").arg(n + 1));
                  _brf.etag();
                  }
            }
      else {
            _brf.stag("direction-type");
            QString tag = "wedge type=";
            if (hp->tick() == tick) {
                  if (hp->hairpinType() == HairpinType::CRESC_HAIRPIN) {
                        tag += "\"crescendo\"";
                        if (hp->hairpinCircledTip()) {
                              tag += " niente=\"yes\"";
                              }
                        }
                  else {
                        tag += "\"diminuendo\"";
                        }
                  }
            else {
                  tag += "\"stop\"";
                  if (hp->hairpinCircledTip() && hp->hairpinType() == HairpinType::DECRESC_HAIRPIN) {
                        tag += " niente=\"yes\"";
                        }
                  }
            tag += QString(" number=\"%1\"").arg(n + 1);
            tag += addPositioningAttributes(hp, hp->tick() == tick);
            _brf.tagE(tag);
            _brf.etag();
            }
      directionETag(_brf, staff);
      }

//---------------------------------------------------------
//   findOttava -- get index of ottava in ottava table
//   return -1 if not found
//---------------------------------------------------------

int ExportBrf::findOttava(const Ottava* ot) const
      {
      for (int i = 0; i < MAX_NUMBER_LEVEL; ++i)
            if (ottavas[i] == ot) return i;
      return -1;
      }

//---------------------------------------------------------
//   ottava
// <octave-shift type="down" size="8" relative-y="14"/>
// <octave-shift type="stop" size="8"/>
//---------------------------------------------------------

void ExportBrf::ottava(Ottava const* const ot, int staff, const Fraction& tick)
      {
      auto n = findOttava(ot);
      if (n >= 0)
            ottavas[n] = 0;
      else {
            n = findOttava(0);
            if (n >= 0)
                  ottavas[n] = ot;
            else {
                  qDebug("too many overlapping ottavas (ot %p staff %d tick %d)", ot, staff, tick.ticks());
                  return;
                  }
            }

      QString octaveShiftBrf;
      const auto st = ot->ottavaType();
      if (ot->tick() == tick) {
            const char* sz = 0;
            const char* tp = 0;
            switch (st) {
                  case OttavaType::OTTAVA_8VA:
                        sz = "8";
                        tp = "down";
                        break;
                  case OttavaType::OTTAVA_15MA:
                        sz = "15";
                        tp = "down";
                        break;
                  case OttavaType::OTTAVA_8VB:
                        sz = "8";
                        tp = "up";
                        break;
                  case OttavaType::OTTAVA_15MB:
                        sz = "15";
                        tp = "up";
                        break;
                  default:
                        qDebug("ottava subtype %d not understood", int(st));
                  }
            if (sz && tp)
                  octaveShiftBrf = QString("octave-shift type=\"%1\" size=\"%2\" number=\"%3\"").arg(tp).arg(sz).arg(n + 1);
            }
      else {
            if (st == OttavaType::OTTAVA_8VA || st == OttavaType::OTTAVA_8VB)
                  octaveShiftBrf = QString("octave-shift type=\"stop\" size=\"8\" number=\"%1\"").arg(n + 1);
            else if (st == OttavaType::OTTAVA_15MA || st == OttavaType::OTTAVA_15MB)
                  octaveShiftBrf = QString("octave-shift type=\"stop\" size=\"15\" number=\"%1\"").arg(n + 1);
            else
                  qDebug("ottava subtype %d not understood", int(st));
            }

      if (octaveShiftBrf != "") {
            directionTag(_brf, _attr, ot);
            _brf.stag("direction-type");
            octaveShiftBrf += addPositioningAttributes(ot, ot->tick() == tick);
            _brf.tagE(octaveShiftBrf);
            _brf.etag();
            directionETag(_brf, staff);
            }
      }

//---------------------------------------------------------
//   pedal
//---------------------------------------------------------

void ExportBrf::pedal(Pedal const* const pd, int staff, const Fraction& tick)
      {
      directionTag(_brf, _attr, pd);
      _brf.stag("direction-type");
      QString pedalBrf;
      if (pd->tick() == tick)
            pedalBrf = "pedal type=\"start\" line=\"yes\"";
      else
            pedalBrf = "pedal type=\"stop\" line=\"yes\"";
      pedalBrf += addPositioningAttributes(pd, pd->tick() == tick);
      _brf.tagE(pedalBrf);
      _brf.etag();
      directionETag(_brf, staff);
      }

//---------------------------------------------------------
//   findBracket -- get index of bracket in bracket table
//   return -1 if not found
//---------------------------------------------------------

int ExportBrf::findBracket(const TextLine* tl) const
      {
      for (int i = 0; i < MAX_NUMBER_LEVEL; ++i)
            if (brackets[i] == tl) return i;
      return -1;
      }

//---------------------------------------------------------
//   textLine
//---------------------------------------------------------

void ExportBrf::textLine(TextLine const* const tl, int staff, const Fraction& tick)
      {
      int n;
      // special case: a dashed line w/o hooks is written as dashes
      const auto isDashes = tl->lineStyle() == Qt::DashLine && (tl->beginHookType() == HookType::NONE) && (tl->endHookType() == HookType::NONE);

      if (isDashes) {
            n = findDashes(tl);
            if (n >= 0)
                  dashes[n] = nullptr;
            else {
                  n = findBracket(nullptr);
                  if (n >= 0)
                        dashes[n] = tl;
                  else {
                        qDebug("too many overlapping dashes (tl %p staff %d tick %d)", tl, staff, tick.ticks());
                        return;
                        }
                  }
            }
      else {
            n = findBracket(tl);
            if (n >= 0)
                  brackets[n] = nullptr;
            else {
                  n = findBracket(nullptr);
                  if (n >= 0)
                        brackets[n] = tl;
                  else {
                        qDebug("too many overlapping textlines (tl %p staff %d tick %d)", tl, staff, tick.ticks());
                        return;
                        }
                  }
            }

      QString rest;
      QPointF p;

      QString lineEnd = "none";
      QString type;
      bool hook = false;
      double hookHeight = 0.0;
      if (tl->tick() == tick) {
            if (!isDashes) {
                  QString lineType;
                  switch (tl->lineStyle()) {
                        case Qt::SolidLine:
                              lineType = "solid";
                              break;
                        case Qt::DashLine:
                              lineType = "dashed";
                              break;
                        case Qt::DotLine:
                              lineType = "dotted";
                              break;
                        default:
                              lineType = "solid";
                        }
                  rest += QString(" line-type=\"%1\"").arg(lineType);
                  }
            hook       = tl->beginHookType() != HookType::NONE;
            hookHeight = tl->beginHookHeight().val();
            if (!tl->segmentsEmpty())
                  p = tl->frontSegment()->offset();
            // offs = tl->mxmlOff();
            type = "start";
            }
      else {
            hook = tl->endHookType() != HookType::NONE;
            hookHeight = tl->endHookHeight().val();
            if (!tl->segmentsEmpty())
                  p = (toLineSegment(tl->backSegment()))->userOff2();
            // offs = tl->mxmlOff2();
            type = "stop";
            }

      if (hook) {
            if (hookHeight < 0.0) {
                  lineEnd = "up";
                  hookHeight *= -1.0;
                  }
            else
                  lineEnd = "down";
            rest += QString(" end-length=\"%1\"").arg(hookHeight * 10);
            }

      rest += addPositioningAttributes(tl, tl->tick() == tick);

      directionTag(_brf, _attr, tl);

      if (!tl->beginText().isEmpty() && tl->tick() == tick) {
            _brf.stag("direction-type");
            _brf.tag("words", tl->beginText());
            _brf.etag();
            }

      _brf.stag("direction-type");
      if (isDashes)
            _brf.tagE(QString("dashes type=\"%1\" number=\"%2\"").arg(type, QString::number(n + 1)));
      else
            _brf.tagE(QString("bracket type=\"%1\" number=\"%2\" line-end=\"%3\"%4").arg(type, QString::number(n + 1), lineEnd, rest));
      _brf.etag();

      if (!tl->endText().isEmpty() && tl->tick() != tick) {
            _brf.stag("direction-type");
            _brf.tag("words", tl->endText());
            _brf.etag();
            }

      /*
      if (offs)
            brf.tag("offset", offs);
      */

      directionETag(_brf, staff);
      }

//---------------------------------------------------------
//   dynamic
//---------------------------------------------------------

// In MuseScore dynamics are essentially user-defined texts, therefore the ones
// supported by MusicXML need to be filtered out. Everything not recognized
// as MusicXML dynamics is written as other-dynamics.

void ExportBrf::dynamic(Dynamic const* const dyn, int staff)
      {
      QSet<QString> set; // the valid MusicXML dynamics
      set << "f" << "ff" << "fff" << "ffff" << "fffff" << "ffffff"
          << "fp" << "fz"
          << "mf" << "mp"
          << "p" << "pp" << "ppp" << "pppp" << "ppppp" << "pppppp"
          << "rf" << "rfz"
          << "sf" << "sffz" << "sfp" << "sfpp" << "sfz";

      directionTag(_brf, _attr, dyn);

      _brf.stag("direction-type");

      QString tagName = "dynamics";
      tagName += addPositioningAttributes(dyn);
      _brf.stag(tagName);
      const QString dynTypeName = dyn->dynamicTypeName();

      if (set.contains(dynTypeName)) {
            _brf.tagE(dynTypeName);
            }
      else if (dynTypeName != "") {
            std::map<ushort, QChar> map;
            map[0xE520] = 'p';
            map[0xE521] = 'm';
            map[0xE522] = 'f';
            map[0xE523] = 'r';
            map[0xE524] = 's';
            map[0xE525] = 'z';
            map[0xE526] = 'n';

            QString dynText = dynTypeName;
            if (dyn->dynamicType() == Dynamic::Type::OTHER)
                  dynText = dyn->plainText();

            // collect consecutive runs of either dynamics glyphs
            // or other characters and write the runs.
            QString text;
            bool inDynamicsSym = false;
            for (const auto ch : dynText) {
                  const auto it = map.find(ch.unicode());
                  if (it != map.end()) {
                        // found a SMUFL single letter dynamics glyph
                        if (!inDynamicsSym) {
                              if (text != "") {
                                    _brf.tag("other-dynamics", text);
                                    text = "";
                                    }
                              inDynamicsSym = true;
                              }
                        text += it->second;
                        }
                  else {
                        // found a non-dynamics character
                        if (inDynamicsSym) {
                              if (text != "") {
                                    if (set.contains(text))
                                          _brf.tagE(text);
                                    else
                                          _brf.tag("other-dynamics", text);
                                    text = "";
                                    }
                              inDynamicsSym = false;
                              }
                        text += ch;
                        }
                  }
            if (text != "") {
                  if (inDynamicsSym && set.contains(text))
                        _brf.tagE(text);
                  else
                        _brf.tag("other-dynamics", text);
                  }
            }

      _brf.etag();

      _brf.etag();

      /*
      int offs = dyn->mxmlOff();
      if (offs)
            brf.tag("offset", offs);
      */
      if (staff)
            _brf.tag("staff", staff);

      if (dyn->velocity() > 0)
            _brf.tagE(QString("sound dynamics=\"%1\"").arg(QString::number(dyn->velocity() * 100.0 / 90.0, 'f', 2)));

      _brf.etag();
      }

//---------------------------------------------------------
//   symbol
//---------------------------------------------------------

// TODO: remove dependency on symbol name and replace by a more stable interface
// changes in sym.cpp r2494 broke MusicXML export of pedals (again)

void ExportBrf::symbol(Symbol const* const sym, int staff)
      {
      QString name = Sym::id2name(sym->sym());
      QString mbrfName = "";
      if (name == "keyboardPedalPed")
            mbrfName = "pedal type=\"start\"";
      else if (name == "keyboardPedalUp")
            mbrfName = "pedal type=\"stop\"";
      else {
            qDebug("ExportBrf::symbol(): %s not supported", qPrintable(name));
            return;
            }
      directionTag(_brf, _attr, sym);
      mbrfName += addPositioningAttributes(sym);
      _brf.stag("direction-type");
      _brf.tagE(mbrfName);
      _brf.etag();
      directionETag(_brf, staff);
      }

//---------------------------------------------------------
//   lyrics
//---------------------------------------------------------

/* deal with lyrics all at once except change all _xml to _brf
      Todo: write lyrics in Braille
            write contracted lyrics
            add lyrics beyond verse 1 to end of score
*/

void ExportBrf::lyrics(const std::vector<Lyrics*>* ll, const int trk)
      {
      for (const Lyrics* l :* ll) {
            if (l && !l->xmlText().isEmpty()) {
                  if ((l)->track() == trk) {
                        QString lyricXml = QString("lyric number=\"%1\"").arg((l)->no() + 1);
                        lyricXml += color2xml(l);
                        lyricXml += addPositioningAttributes(l);
                        _brf.stag(lyricXml);
                        Lyrics::Syllabic syl = (l)->syllabic();
                        QString s = "";
                        switch (syl) {
                              case Lyrics::Syllabic::SINGLE: s = "single"; break;
                              case Lyrics::Syllabic::BEGIN:  s = "begin";  break;
                              case Lyrics::Syllabic::END:    s = "end";    break;
                              case Lyrics::Syllabic::MIDDLE: s = "middle"; break;
                              default:
                                    qDebug("unknown syllabic %d", int(syl));
                              }
                        _brf.tag("syllabic", s);
                        QString attr; // TODO TBD
                        // set the default words format
                        const QString mtf       = _score->styleSt(Sid::MusicalTextFont);
                        CharFormat defFmt;
                        defFmt.setFontFamily(_score->styleSt(Sid::lyricsEvenFontFace));
                        defFmt.setFontSize(_score->styleD(Sid::lyricsOddFontSize));
                        // write formatted
                        MScoreTextToBraille mttm("text", attr, defFmt, mtf);
                        mttm.writeTextFragments(l->fragmentList(), _brf);
#if 0
                        /*
                         Temporarily disabled because it doesn't work yet (and thus breaks the regression test).
                         See MusicXml::xmlLyric: "// TODO-WS      l->setTick(tick);"
                        if((l)->endTick() > 0)
                              _brf.tagE("extend");
                        */
#else
                        if (l->ticks().isNotZero())
                              _brf.tagE("extend");
#endif
                        _brf.etag();
                        }
                  }
            }
      }

//---------------------------------------------------------
//   directionJump -- write jump
//---------------------------------------------------------

// LVIFIX: TODO coda and segno should be numbered uniquely

static void directionJump(BrfWriter& brf, const Jump* const jp)
      {
      Jump::Type jtp = jp->jumpType();
      QString words = "";
      QString type  = "";
      QString sound = "";
      if (jtp == Jump::Type::DC) {
            if (jp->xmlText() == "")  // deal with jump text later
                  words = "D.C.";
            else
                  words = jp->xmlText();
            sound = "dacapo=\"yes\"";
            }
      else if (jtp == Jump::Type::DC_AL_FINE) {
            if (jp->xmlText() == "")
                  words = "D.C. al Fine";
            else
                  words = jp->xmlText();
            sound = "dacapo=\"yes\"";
            }
      else if (jtp == Jump::Type::DC_AL_CODA) {
            if (jp->xmlText() == "")
                  words = "D.C. al Coda";
            else
                  words = jp->xmlText();
            sound = "dacapo=\"yes\"";
            }
      else if (jtp == Jump::Type::DS_AL_CODA) {
            if (jp->xmlText() == "")
                  words = "D.S. al Coda";
            else
                  words = jp->xmlText();
            if (jp->jumpTo() == "")
                  sound = "dalsegno=\"1\"";
            else
                  sound = "dalsegno=\"" + jp->jumpTo() + "\"";
            }
      else if (jtp == Jump::Type::DS_AL_FINE) {
            if (jp->xmlText() == "")
                  words = "D.S. al Fine";
            else
                  words = jp->xmlText();
            if (jp->jumpTo() == "")
                  sound = "dalsegno=\"1\"";
            else
                  sound = "dalsegno=\"" + jp->jumpTo() + "\"";
            }
      else if (jtp == Jump::Type::DS) {
            words = "D.S.";
            if (jp->jumpTo() == "")
                  sound = "dalsegno=\"1\"";
            else
                  sound = "dalsegno=\"" + jp->jumpTo() + "\"";
            }
      else
            qDebug("jump type=%d not implemented", static_cast<int>(jtp));

      if (sound != "") {
            brf.stag(QString("direction placement=\"%1\"").arg((jp->placement() == Placement::BELOW ) ? "below" : "above"));
            brf.stag("direction-type");
            QString positioning = "";
            positioning += addPositioningAttributes(jp);
            if (type != "") brf.tagE(type + positioning);
            if (words != "") brf.tag("words" + positioning, words);
            brf.etag();
            if (sound != "") brf.tagE(QString("sound ") + sound);
            brf.etag();
            }
      }

//---------------------------------------------------------
//   directionMarker -- write marker
//---------------------------------------------------------

static void directionMarker(BrfWriter& brf, const Marker* const m)
      {
      Marker::Type mtp = m->markerType();  // deal with marker text later
      QString words = "";
      QString type  = "";
      QString sound = "";
      if (mtp == Marker::Type::CODA) {
            type = "coda";
            if (m->label() == "")
                  sound = "coda=\"1\"";
            else
                  // LVIFIX hack: force label to "coda" to match to coda label
                  // sound = "coda=\"" + m->label() + "\"";
                  sound = "coda=\"coda\"";
            }
      else if (mtp == Marker::Type::SEGNO) {
            type = "segno";
            if (m->label() == "")
                  sound = "segno=\"1\"";
            else
                  sound = "segno=\"" + m->label() + "\"";
            }
      else if (mtp == Marker::Type::FINE) {
            words = "Fine";
            sound = "fine=\"yes\"";
            }
      else if (mtp == Marker::Type::TOCODA) {
            if (m->xmlText() == "")
                  words = "To Coda";
            else
                  words = m->xmlText();
            if (m->label() == "")
                  sound = "tocoda=\"1\"";
            else
                  sound = "tocoda=\"" + m->label() + "\"";
            }
      else
            qDebug("marker type=%d not implemented", int(mtp));

      if (sound != "") {
            brf.stag(QString("direction placement=\"%1\"").arg((m->placement() == Placement::BELOW ) ? "below" : "above"));
            brf.stag("direction-type");
            QString positioning = "";
            positioning += addPositioningAttributes(m);
            if (type != "") brf.tagE(type + positioning);
            if (words != "") brf.tag("words" + positioning, words);
            brf.etag();
            if (sound != "") brf.tagE(QString("sound ") + sound);
            brf.etag();
            }
      }

//---------------------------------------------------------
//  findTrackForAnnotations
//---------------------------------------------------------

// An annotation is attached to the staff, with track set
// to the lowest track in the staff. Find a track for it
// (the lowest track in this staff that has a chord or rest)

static int findTrackForAnnotations(int track, Segment* seg)
      {
      if (seg->segmentType() != SegmentType::ChordRest)
            return -1;

      int staff = track / VOICES;
      int strack = staff * VOICES;      // start track of staff containing track
      int etrack = strack + VOICES;     // end track of staff containing track + 1

      for (int i = strack; i < etrack; i++)
            if (seg->element(i))
                  return i;

      return -1;
      }

//---------------------------------------------------------
//  repeatAtMeasureStart -- write repeats at begin of measure
//---------------------------------------------------------

static void repeatAtMeasureStart(BrfWriter& brf, Attributes& attr, Measure* m, int strack, int etrack, int track)
      {
      // loop over all segments
      for (Element* e : m->el()) {
            int wtrack = -1; // track to write jump
            if (strack <= e->track() && e->track() < etrack)
                  wtrack = findTrackForAnnotations(e->track(), m->first(SegmentType::ChordRest));
            if (track != wtrack)
                  continue;
            switch (e->type()) {
                  case ElementType::MARKER:
                        {
                        // filter out the markers at measure Start
                        const Marker* const mk = static_cast<const Marker* const>(e);
                        Marker::Type mtp = mk->markerType();
                        if (   mtp == Marker::Type::SEGNO
                               || mtp == Marker::Type::CODA
                               ) {
                              qDebug(" -> handled");
                              attr.doAttr(brf, false);
                              directionMarker(brf, mk);
                              }
                        else if (   mtp == Marker::Type::FINE
                                    || mtp == Marker::Type::TOCODA
                                    ) {
                              // ignore
                              }
                        else {
                              qDebug("repeatAtMeasureStart: marker %d not implemented", int(mtp));
                              }
                        }
                        break;
                  default:
                        qDebug("repeatAtMeasureStart: direction type %s at tick %d not implemented",
                               Element::name(e->type()), m->tick().ticks());
                        break;
                  }
            }
      }

//---------------------------------------------------------
//  repeatAtMeasureStop -- write repeats at end of measure
//---------------------------------------------------------

static void repeatAtMeasureStop(BrfWriter& brf, Measure* m, int strack, int etrack, int track)
      {
      for (Element* e : m->el()) {
            int wtrack = -1; // track to write jump
            if (strack <= e->track() && e->track() < etrack)
                  wtrack = findTrackForAnnotations(e->track(), m->first(SegmentType::ChordRest));
            if (track != wtrack)
                  continue;
            switch (e->type()) {
                  case ElementType::MARKER:
                        {
                        // filter out the markers at measure stop
                        const Marker* const mk = static_cast<const Marker* const>(e);
                        Marker::Type mtp = mk->markerType();
                        if (mtp == Marker::Type::FINE || mtp == Marker::Type::TOCODA) {
                              directionMarker(brf, mk);
                              }
                        else if (mtp == Marker::Type::SEGNO || mtp == Marker::Type::CODA) {
                              // ignore
                              }
                        else {
                              qDebug("repeatAtMeasureStop: marker %d not implemented", int(mtp));
                              }
                        }
                        break;
                  case ElementType::JUMP:
                        directionJump(brf, static_cast<const Jump* const>(e));
                        break;
                  default:
                        qDebug("repeatAtMeasureStop: direction type %s at tick %d not implemented",
                               Element::name(e->type()), m->tick().ticks());
                        break;
                  }
            }
      }

//---------------------------------------------------------
//  work -- write the <work> element
//  note that order must be work-number, work-title
//  also write <movement-number> and <movement-title>
//  data is taken from the score metadata instead of the Text elements
//---------------------------------------------------------

void ExportBrf::work(const MeasureBase* /*measure*/)
      {
      QString workTitle  = _score->metaTag("workTitle");
      QString workNumber = _score->metaTag("workNumber");
      if (!(workTitle.isEmpty() && workNumber.isEmpty())) {
            _brf.stag("work");
            if (!workNumber.isEmpty())
                  _brf.tag("work-number", workNumber);
            if (!workTitle.isEmpty())
                  _brf.tag("work-title", workTitle);
            _brf.etag();
            }
      if (!_score->metaTag("movementNumber").isEmpty())
            _brf.tag("movement-number", _score->metaTag("movementNumber"));
      if (!_score->metaTag("movementTitle").isEmpty())
            _brf.tag("movement-title", _score->metaTag("movementTitle"));
      }

#if 0
//---------------------------------------------------------
//   elementRighter // used for harmony order
//---------------------------------------------------------

static bool elementRighter(const Element* e1, const Element* e2)
      {
      return e1->x() < e2->x();
      }
#endif

//---------------------------------------------------------
//  measureStyle -- write measure-style
//---------------------------------------------------------

// this is done at the first measure of a multi-meaure rest
// note: for a normal measure, mmRest1 is the measure itself,
// for a multi-meaure rest, it is the replacing measure

static void measureStyle(BrfWriter& brf, Attributes& attr, Measure* m)
      {
      const Measure* mmR1 = m->mmRest1();
      if (m != mmR1 && m == mmR1->mmRestFirst()) {
            attr.doAttr(brf, true);
            brf.stag("measure-style");
            brf.tag("multiple-rest", mmR1->mmRestCount());
            brf.etag();
            }
      }

//---------------------------------------------------------
//  findFretDiagram
//---------------------------------------------------------

static const FretDiagram* findFretDiagram(int strack, int etrack, int track, Segment* seg)
      {
      if (seg->segmentType() == SegmentType::ChordRest) {
            for (const Element* e : seg->annotations()) {

                  int wtrack = -1; // track to write annotation

                  if (strack <= e->track() && e->track() < etrack)
                        wtrack = findTrackForAnnotations(e->track(), seg);

                  if (track == wtrack && e->type() == ElementType::FRET_DIAGRAM)
                        return static_cast<const FretDiagram*>(e);
                  }
            }
      return 0;
      }

//---------------------------------------------------------
//  commonAnnotations
//---------------------------------------------------------

static bool commonAnnotations(ExportBrf* exp, const Element* e, int sstaff)
      {
      if (e->isSymbol())
            exp->symbol(toSymbol(e), sstaff);
      else if (e->isTempoText())
            exp->tempoText(toTempoText(e), sstaff);
      else if (e->isStaffText() || e->isSystemText() || e->isText() || e->isInstrumentChange())
            exp->words(toTextBase(e), sstaff);
      else if (e->isDynamic())
            exp->dynamic(toDynamic(e), sstaff);
      else if (e->isRehearsalMark())
            exp->rehearsal(toRehearsalMark(e), sstaff);
      else
            return false;

      return true;
      }

//---------------------------------------------------------
//  annotations
//---------------------------------------------------------

// In MuseScore, Element::FRET_DIAGRAM and Element::HARMONY are separate annotations,
// in MusicXML they are combined in the harmony element. This means they have to be matched.
// TODO: replace/repair current algorithm (which can only handle one FRET_DIAGRAM and one HARMONY)

static void annotations(ExportBrf* exp, int strack, int etrack, int track, int sstaff, Segment* seg)
      {
      if (seg->segmentType() == SegmentType::ChordRest) {

            const FretDiagram* fd = findFretDiagram(strack, etrack, track, seg);
            // if (fd) qDebug("annotations seg %p found fretboard diagram %p", seg, fd);

            for (const Element* e : seg->annotations()) {

                  int wtrack = -1; // track to write annotation

                  if (strack <= e->track() && e->track() < etrack)
                        wtrack = findTrackForAnnotations(e->track(), seg);

                  if (track == wtrack) {
                        if (commonAnnotations(exp, e, sstaff))
                              ;  // already handled
                        else if (e->isHarmony()) {
                              // qDebug("annotations seg %p found harmony %p", seg, e);
                              exp->harmony(toHarmony(e), fd);
                              fd = nullptr; // make sure to write only once ...
                              }
                        else if (e->isFermata() || e->isFiguredBass() || e->isFretDiagram() || e->isJump())
                              ;  // handled separately by chordAttributes(), figuredBass(), findFretDiagram() or ignored
                        else
                              qDebug("annotations: direction type %s at tick %d not implemented",
                                     Element::name(e->type()), seg->tick().ticks());
                        }
                  }
            if (fd)
                  // found fd but no harmony, cannot write (MusicXML would be invalid)
                  qDebug("annotations seg %p found fretboard diagram %p w/o harmony: cannot write",
                         seg, fd);
            }
      }

//---------------------------------------------------------
//  figuredBass
//---------------------------------------------------------

static void figuredBass(BrfWriter& brf, int strack, int etrack, int track, const ChordRest* cr, FigBassMap& fbMap, int divisions)
      {
      Segment* seg = cr->segment();
      if (seg->segmentType() == SegmentType::ChordRest) {
            for (const Element* e : seg->annotations()) {

                  int wtrack = -1; // track to write annotation

                  if (strack <= e->track() && e->track() < etrack)
                        wtrack = findTrackForAnnotations(e->track(), seg);

                  if (track == wtrack) {
                        if (e->type() == ElementType::FIGURED_BASS) {
                              const FiguredBass* fb = dynamic_cast<const FiguredBass*>(e);  // deal with figured base text later
                              //qDebug("figuredbass() track %d seg %p fb %p seg %p tick %d ticks %d cr %p tick %d ticks %d",
                              //       track, seg, fb, fb->segment(), fb->segment()->tick(), fb->ticks(), cr, cr->tick(), cr->actualTicks());
                              bool extend = fb->ticks() > cr->actualTicks();   // deal with figured bass later
                              if (extend) {
                                    //qDebug("figuredbass() extend to %d + %d = %d",
                                    //       cr->tick(), fb->ticks(), cr->tick() + fb->ticks());
                                    fbMap.insert(strack, fb);
                                    }
                              else
                                    fbMap.remove(strack);
                              const Fraction crEndTick = cr->tick() + cr->actualTicks();
                              const Fraction fbEndTick = fb->segment()->tick() + fb->ticks();
                              const bool writeDuration = fb->ticks() < cr->actualTicks();
                              fb->writeMusicXML(brf, true, crEndTick.ticks(), fbEndTick.ticks(),
                                                writeDuration, divisions);

                              // Check for changing figures under a single note (each figure stored in a separate segment)
                              for (Segment* segNext = seg->next(); segNext && segNext->element(track) == NULL; segNext = segNext->next()) {
                                    for (Element* annot : segNext->annotations()) {
                                          if (annot->type() == ElementType::FIGURED_BASS && annot->track() == track) {
                                                fb = dynamic_cast<const FiguredBass*>(annot);
                                                fb->writeMusicXML(brf, true, 0, 0, true, divisions);
                                                }
                                          }
                                    }
                              // no extend can be pending
                              return;
                              }
                        }
                  }
            // check for extend pending
            if (fbMap.contains(strack)) {
                  const FiguredBass* fb = fbMap.value(strack);
                  Fraction crEndTick = cr->tick() + cr->actualTicks();
                  Fraction fbEndTick = fb->segment()->tick() + fb->ticks();
                  bool writeDuration = fb->ticks() < cr->actualTicks();
                  if (cr->tick() < fbEndTick) {
                        //qDebug("figuredbass() at tick %d extend only", cr->tick());
                        fb->writeMusicXML(brf, false, crEndTick.ticks(), fbEndTick.ticks(), writeDuration, divisions);
                        }
                  if (fbEndTick <= crEndTick) {
                        //qDebug("figuredbass() at tick %d extend done", cr->tick() + cr->actualTicks());
                        fbMap.remove(strack);
                        }
                  }
            }
      }

//---------------------------------------------------------
//  spannerStart
//---------------------------------------------------------

// for each spanner start:
// find start track
// find stop track
// if stop track < start track
//   get data from list of already stopped spanners
// else
//   calculate data
// write start if in right track

static void spannerStart(ExportBrf* exp, int strack, int etrack, int track, int sstaff, Segment* seg)
      {
      if (seg->segmentType() == SegmentType::ChordRest) {
            Fraction stick = seg->tick();
            for (auto it = exp->score()->spanner().lower_bound(stick.ticks()); it != exp->score()->spanner().upper_bound(stick.ticks()); ++it) {
                  Spanner* e = it->second;

                  int wtrack = -1; // track to write spanner
                  if (strack <= e->track() && e->track() < etrack)
                        wtrack = findTrackForAnnotations(e->track(), seg);

                  if (track == wtrack) {
                        switch (e->type()) {
                              case ElementType::HAIRPIN:
                                    exp->hairpin(static_cast<const Hairpin*>(e), sstaff, seg->tick());
                                    break;
                              case ElementType::OTTAVA:
                                    exp->ottava(static_cast<const Ottava*>(e), sstaff, seg->tick());
                                    break;
                              case ElementType::PEDAL:
                                    exp->pedal(static_cast<const Pedal*>(e), sstaff, seg->tick());
                                    break;
                              case ElementType::TEXTLINE:
                                    exp->textLine(static_cast<const TextLine*>(e), sstaff, seg->tick());
                                    break;
                              case ElementType::TRILL:
                                    // ignore (written as <note><notations><ornaments><wavy-line>)
                                    break;
                              case ElementType::SLUR:
                                    // ignore (written as <note><notations><slur>)
                                    break;
                              default:
                                    qDebug("spannerStart: direction type %s at tick %d not implemented",
                                           Element::name(e->type()), seg->tick().ticks());
                                    break;
                              }
                        }
                  } // for
            }
      }

//---------------------------------------------------------
//  spannerStop
//---------------------------------------------------------

// called after writing each chord or rest to check if a spanner must be stopped
// loop over all spanners and find spanners in strack ending at tick2
// note that more than one voice may contains notes ending at tick2,
// remember which spanners have already been stopped (the "stopped" set)

static void spannerStop(ExportBrf* exp, int strack, const Fraction& tick2, int sstaff, QSet<const Spanner*>& stopped)
      {
      for (auto it : exp->score()->spanner()) {
            Spanner* e = it.second;

            if (e->tick2() != tick2 || e->track() != strack)
                  continue;

            if (!stopped.contains(e)) {
                  stopped.insert(e);
                  switch (e->type()) {
                        case ElementType::HAIRPIN:
                              exp->hairpin(static_cast<const Hairpin*>(e), sstaff, Fraction(-1,1));
                              break;
                        case ElementType::OTTAVA:
                              exp->ottava(static_cast<const Ottava*>(e), sstaff, Fraction(-1,1));
                              break;
                        case ElementType::PEDAL:
                              exp->pedal(static_cast<const Pedal*>(e), sstaff, Fraction(-1,1));
                              break;
                        case ElementType::TEXTLINE:
                              exp->textLine(static_cast<const TextLine*>(e), sstaff, Fraction(-1,1));
                              break;
                        case ElementType::TRILL:
                              // ignore (written as <note><notations><ornaments><wavy-line>
                              break;
                        case ElementType::SLUR:
                              // ignore (written as <note><notations><slur>)
                              break;
                        default:
                              qDebug("spannerStop: direction type %s at tick2 %d not implemented",
                                     Element::name(e->type()), tick2.ticks());
                              break;
                        }
                  }
            } // for
      }

//---------------------------------------------------------
//  keysigTimesig
//---------------------------------------------------------

/**
 Output attributes at start of measure: key, time
 */

void ExportBrf::keysigTimesig(const Measure* m, const Part* p)
      {
      int strack = p->startTrack();
      int etrack = p->endTrack();
      //qDebug("keysigTimesig m %p strack %d etrack %d", m, strack, etrack);

      // search all staves for non-generated key signatures
      QMap<int, KeySig*> keysigs; // map staff to key signature
      for (Segment* seg = m->first(); seg; seg = seg->next()) {
            if (seg->tick() > m->tick())
                  break;
            for (int t = strack; t < etrack; t += VOICES) {
                  Element* el = seg->element(t);
                  if (!el)
                        continue;
                  if (el->type() == ElementType::KEYSIG) {
                        //qDebug(" found keysig %p track %d", el, el->track());
                        int st = (t - strack) / VOICES;
                        if (!el->generated())
                              keysigs[st] = static_cast<KeySig*>(el);
                        }
                  }
            }

      //ClefType ct = rest->staff()->clef(rest->tick());

      // write the key signatues
      if (!keysigs.isEmpty()) {
            // determine if all staves have a keysig and all keysigs are identical
            // in that case a single <key> is written, without number=... attribute
            int nstaves = p->nstaves();
            bool singleKey = true;
            // check if all staves have a keysig
            for (int i = 0; i < nstaves; i++)
                  if (!keysigs.contains(i))
                        singleKey = false;
            // check if all keysigs are identical
            if (singleKey)
                  for (int i = 1; i < nstaves; i++)
                        if (!(keysigs.value(i)->key() == keysigs.value(0)->key()))
                              singleKey = false;

            // write the keysigs
            //qDebug(" singleKey %d", singleKey);
            if (singleKey) {
                  // keysig applies to all staves
                  keysig(keysigs.value(0), p->staff(0)->clef(m->tick()), 0, keysigs.value(0)->visible());
                  }
            else {
                  // staff-specific keysigs
                  for (int st : keysigs.keys())
                        keysig(keysigs.value(st), p->staff(st)->clef(m->tick()), st + 1, keysigs.value(st)->visible());
                  }
            }
      else {
            // always write a keysig at tick = 0
            if (m->tick().isZero()) {
                  //KeySigEvent kse;
                  //kse.setKey(Key::C);
                  KeySig* ks = new KeySig(_score);
                  ks->setKey(Key::C);
                  keysig(ks, p->staff(0)->clef(m->tick()));
                  delete ks;
                  }
            }

      TimeSig* tsig = 0;
      for (Segment* seg = m->first(); seg; seg = seg->next()) {
            if (seg->tick() > m->tick())
                  break;
            Element* el = seg->element(strack);
            if (el && el->type() == ElementType::TIMESIG)
                  tsig = (TimeSig*) el;
            }
      if (tsig)
            timesig(tsig);
      }

//---------------------------------------------------------
//  identification -- write the identification
//---------------------------------------------------------

static void identification(BrfWriter& brf, Score const* const score)
      {
      brf.stag("identification");

      QStringList creators;
      // the creator types commonly found in MusicXML
      creators << "arranger" << "composer" << "lyricist" << "poet" << "translator";
      for (QString type : creators) {
            QString creator = score->metaTag(type);
            if (!creator.isEmpty())
                  brf.tag(QString("creator type=\"%1\"").arg(type), creator);
            }

      if (!score->metaTag("copyright").isEmpty())
            brf.tag("rights", score->metaTag("copyright"));

      brf.stag("encoding");

      if (MScore::debugMode) {
            brf.tag("software", QString("MuseScore 0.7.0"));
            brf.tag("encoding-date", QString("2007-09-10"));
            }
      else {
            brf.tag("software", QString("MuseScore ") + QString(VERSION));
            brf.tag("encoding-date", QDate::currentDate().toString(Qt::ISODate));
            }

      // specify supported elements
      brf.tagE("supports element=\"accidental\" type=\"yes\"");
      brf.tagE("supports element=\"beam\" type=\"yes\"");
      // set support for print new-page and new-system to match user preference
      // for MusicxmlExportBreaks::MANUAL support is "no" because "yes" breaks Finale NotePad import
      if (preferences.getBool(PREF_EXPORT_BRAILLE_EXPORTLAYOUT)
          && preferences.musicxmlExportBreaks() == MusicxmlExportBreaks::ALL) {
            brf.tagE("supports element=\"print\" attribute=\"new-page\" type=\"yes\" value=\"yes\"");
            brf.tagE("supports element=\"print\" attribute=\"new-system\" type=\"yes\" value=\"yes\"");
            }
      else {
            brf.tagE("supports element=\"print\" attribute=\"new-page\" type=\"no\"");
            brf.tagE("supports element=\"print\" attribute=\"new-system\" type=\"no\"");
            }
      brf.tagE("supports element=\"stem\" type=\"yes\"");

      brf.etag();

      if (!score->metaTag("source").isEmpty())
            brf.tag("source", score->metaTag("source"));

      brf.etag();
      }

//---------------------------------------------------------
//  findPartGroupNumber
//---------------------------------------------------------

static int findPartGroupNumber(int* partGroupEnd)
      {
      // find part group number
      for (int number = 0; number < MAX_PART_GROUPS; ++number)
            if (partGroupEnd[number] == -1)
                  return number;
      qDebug("no free part group number");
      return MAX_PART_GROUPS;
      }

//---------------------------------------------------------
//  scoreInstrument
//---------------------------------------------------------

static void scoreInstrument(BrfWriter& brf, const int partNr, const int instrNr, const QString& instrName)
      {
      brf.stag(QString("score-instrument %1").arg(instrId(partNr, instrNr)));
      brf.tag("instrument-name", instrName);
      brf.etag();
      }

//---------------------------------------------------------
//  midiInstrument
//---------------------------------------------------------

static void midiInstrument(BrfWriter& brf, const int partNr, const int instrNr,
                           const Instrument* instr, const Score* score, const int unpitched = 0)
      {
      brf.stag(QString("midi-instrument %1").arg(instrId(partNr, instrNr)));
      int midiChannel = score->masterScore()->midiChannel(instr->channel(0)->channel());
      if (midiChannel >= 0 && midiChannel < 16)
            brf.tag("midi-channel", midiChannel + 1);
      int midiProgram = instr->channel(0)->program();
      if (midiProgram >= 0 && midiProgram < 128)
            brf.tag("midi-program", midiProgram + 1);
      if (unpitched > 0)
            brf.tag("midi-unpitched", unpitched);
      brf.tag("volume", (instr->channel(0)->volume() / 127.0) * 100);  //percent
      brf.tag("pan", int(((instr->channel(0)->pan() - 63.5) / 63.5) * 90)); //-90 hard left, +90 hard right      brf.etag();
      brf.etag();
      }

//---------------------------------------------------------
//  initInstrMap
//---------------------------------------------------------

/**
 Initialize the Instrument* to number map for a Part
 Used to generate instrument numbers for a multi-instrument part
 */

static void initInstrMap(MbrfInstrumentMap& im, const InstrumentList* il, const Score* /*score*/)
      {
      im.clear();
      for (auto i = il->begin(); i != il->end(); ++i) {
            const Instrument* pinstr = i->second;
            if (!im.contains(pinstr))
                  im.insert(pinstr, im.size());
            }
      }

//---------------------------------------------------------
//  initReverseInstrMap
//---------------------------------------------------------

typedef QMap<int, const Instrument*> MbrfReverseInstrumentMap;

/**
 Initialize the number t Instrument* map for a Part
 Used to iterate in sequence over instrument numbers for a multi-instrument part
 */

static void initReverseInstrMap(MbrfReverseInstrumentMap& rim, const MbrfInstrumentMap& im)
      {
      rim.clear();
      for (const Instrument* i : im.keys()) {
            int instNr = im.value(i);
            rim.insert(instNr, i);
            }
      }

//---------------------------------------------------------
//  print
//---------------------------------------------------------

/**
 Handle the <print> element.
 When exporting layout and all breaks, a <print> with layout information
 is generated for the measure types TopSystem, NewSystem and newPage.
 When exporting layout but only manual or no breaks, a <print> with
 layout information is generated only for the measure type TopSystem,
 as it is assumed the system layout is broken by the importing application
 anyway and is thus useless.
 */

void ExportBrf::print(const Measure* const m, const int partNr, const int firstStaffOfPart, const int nrStavesInPart)
      {
      int currentSystem = NoSystem;
      Measure* previousMeasure = 0;

      for (MeasureBase* currentMeasureB = m->prev(); currentMeasureB; currentMeasureB = currentMeasureB->prev()) {
            if (currentMeasureB->type() == ElementType::MEASURE) {
                  previousMeasure = (Measure*) currentMeasureB;
                  break;
                  }
            }

      if (!previousMeasure)
            currentSystem = TopSystem;
      else {
            const auto mSystem = m->mmRest1()->system();
            const auto previousMeasureSystem = previousMeasure->mmRest1()->system();

            if (mSystem && previousMeasureSystem) {
                  if (mSystem->page() != previousMeasureSystem->page())
                        currentSystem = NewPage;
                  else if (mSystem != previousMeasureSystem)
                        currentSystem = NewSystem;
                  }
            }

      bool prevMeasLineBreak = false;
      bool prevMeasPageBreak = false;
      bool prevMeasSectionBreak = false;
      if (previousMeasure) {
            prevMeasLineBreak = previousMeasure->lineBreak();
            prevMeasPageBreak = previousMeasure->pageBreak();
            prevMeasSectionBreak = previousMeasure->sectionBreak();
            }

      if (currentSystem != NoSystem) {

            // determine if a new-system or new-page is required
            QString newThing;       // new-[system|page]="yes" or empty
            if (preferences.musicxmlExportBreaks() == MusicxmlExportBreaks::ALL) {
                  if (currentSystem == NewSystem)
                        newThing = " new-system=\"yes\"";
                  else if (currentSystem == NewPage)
                        newThing = " new-page=\"yes\"";
                  }
            else if (preferences.musicxmlExportBreaks() == MusicxmlExportBreaks::MANUAL) {
                  if (currentSystem == NewSystem && (prevMeasLineBreak || prevMeasSectionBreak))
                        newThing = " new-system=\"yes\"";
                  else if (currentSystem == NewPage && prevMeasPageBreak)
                        newThing = " new-page=\"yes\"";
                  }

            // determine if layout information is required
            bool doLayout = false;
            if (preferences.getBool(PREF_EXPORT_BRAILLE_EXPORTLAYOUT)) {
                  if (currentSystem == TopSystem
                      || (preferences.musicxmlExportBreaks() == MusicxmlExportBreaks::ALL && newThing != "")) {
                        doLayout = true;
                        }
                  }

            if (doLayout) {
                  _brf.stag(QString("print%1").arg(newThing));
                  const double pageWidth  = getTenthsFromInches(score()->styleD(Sid::pageWidth));
                  const double lm = getTenthsFromInches(score()->styleD(Sid::pageOddLeftMargin));
                  const double rm = getTenthsFromInches(score()->styleD(Sid::pageWidth)
                                                        - score()->styleD(Sid::pagePrintableWidth) - score()->styleD(Sid::pageOddLeftMargin));
                  const double tm = getTenthsFromInches(score()->styleD(Sid::pageOddTopMargin));

                  // System Layout

                  // For a multi-meaure rest positioning is valid only
                  // in the replacing measure
                  // note: for a normal measure, mmRest1 is the measure itself,
                  // for a multi-meaure rest, it is the replacing measure
                  const Measure* mmR1 = m->mmRest1();
                  const System* system = mmR1->system();

                  // Put the system print suggestions only for the first part in a score...
                  if (partNr == 0) {

                        // Find the right margin of the system.
                        double systemLM = getTenthsFromDots(mmR1->pagePos().x() - system->page()->pagePos().x()) - lm;
                        double systemRM = pageWidth - rm - (getTenthsFromDots(system->bbox().width()) + lm);

                        _brf.stag("system-layout");
                        _brf.stag("system-margins");
                        _brf.tag("left-margin", QString("%1").arg(QString::number(systemLM,'f',2)));
                        _brf.tag("right-margin", QString("%1").arg(QString::number(systemRM,'f',2)) );
                        _brf.etag();

                        if (currentSystem == NewPage || currentSystem == TopSystem) {
                              const double topSysDist = getTenthsFromDots(mmR1->pagePos().y()) - tm;
                              _brf.tag("top-system-distance", QString("%1").arg(QString::number(topSysDist,'f',2)) );
                              }
                        if (currentSystem == NewSystem) {
                              // see System::layout2() for the factor 2 * score()->spatium()
                              const double sysDist = getTenthsFromDots(mmR1->pagePos().y()
                                                                       - previousMeasure->pagePos().y()
                                                                       - previousMeasure->bbox().height()
                                                                       + 2 * score()->spatium()
                                                                       );
                              _brf.tag("system-distance",
                                       QString("%1").arg(QString::number(sysDist,'f',2)));
                              }

                        _brf.etag();
                        }

                  // Staff layout elements.
                  for (int staffIdx = (firstStaffOfPart == 0) ? 1 : 0; staffIdx < nrStavesInPart; staffIdx++) {

                        // calculate distance between this and previous staff using the bounding boxes
                        const auto staffNr = firstStaffOfPart + staffIdx;
                        const auto prevBbox = system->staff(staffNr - 1)->bbox();
                        const auto staffDist = system->staff(staffNr)->bbox().y() - prevBbox.y() - prevBbox.height();

                        _brf.stag(QString("staff-layout number=\"%1\"").arg(staffIdx + 1));
                        _brf.tag("staff-distance", QString("%1").arg(QString::number(getTenthsFromDots(staffDist),'f',2)));
                        _brf.etag();
                        }

                  _brf.etag();
                  }
            else {
                  // !doLayout
                  if (newThing != "")
                        _brf.tagE(QString("print%1").arg(newThing));
                  }

            } // if (currentSystem ...

      }

//---------------------------------------------------------
//  exportDefaultClef
//---------------------------------------------------------

/**
 In case no clef is found, export a default clef with type determined by staff type.
 Note that a multi-measure rest starting in the first measure should be handled correctly.
 */

void ExportBrf::exportDefaultClef(const Part* const part, const Measure* const m)
      {
      const auto staves = part->nstaves();

      if (m->tick() == Fraction(0,1)) {
            const auto clefSeg = m->findSegment(SegmentType::HeaderClef, Fraction(0,1));

            if (clefSeg) {
                  for (int i = 0; i < staves; ++i) {

                        // sstaff - xml staff number, counting from 1 for this
                        // instrument
                        // special number 0 -> don’t show staff number in
                        // xml output (because there is only one staff)

                        auto sstaff = (staves > 1) ? i + 1 : 0;
                        auto track = part->startTrack() + VOICES * i;

                        if (clefSeg->element(track) == nullptr) {
                              ClefType ct { ClefType::G };
                              QString stafftype;
                              switch (part->staff(i)->staffType(Fraction(0,1))->group()) {
                                    case StaffGroup::TAB:
                                          ct = ClefType::TAB;
                                          stafftype = "tab";
                                          break;
                                    case StaffGroup::STANDARD:
                                          ct = ClefType::G;
                                          stafftype = "std";
                                          break;
                                    case StaffGroup::PERCUSSION:
                                          ct = ClefType::PERC;
                                          stafftype = "perc";
                                          break;
                                    }
                              qDebug("no clef found in first measure track %d (stafftype %s)", track, qPrintable(stafftype));
                              clef(sstaff, ct, " print-object=\"no\"");
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//  findAndExportClef
//---------------------------------------------------------

/**
 Make sure clefs at end of measure get exported at start of next measure.
 */

void ExportBrf::findAndExportClef(Measure* m, const int staves, const int strack, const int etrack)
      {
      Measure* prevMeasure = m->prevMeasure();
      Measure* mmR         = m->mmRest();       // the replacing measure in a multi-measure rest
      Fraction tick        = m->tick();
      Segment* cs1;
      Segment* cs2         = m->findSegment(SegmentType::Clef, tick);
      Segment* cs3;
      Segment* seg         = 0;

      if (prevMeasure)
            cs1 = prevMeasure->findSegment(SegmentType::Clef, tick);
      else
            cs1 = m->findSegment(SegmentType::HeaderClef, tick);

      if (mmR) {
            cs3 = mmR->findSegment(SegmentType::HeaderClef, tick);
            if (!cs3)
                  cs3 = mmR->findSegment(SegmentType::Clef, tick);
            }
      else
            cs3 = 0;

      if (cs1 && cs2) {
            // should only happen at begin of new system
            // when previous system ends with a non-generated clef
            seg = cs1;
            }
      else if (cs1)
            seg = cs1;
      else if (cs3) {
            // happens when the first measure is a multi-measure rest
            // containing a generated clef
            seg = cs3;
            }
      else
            seg = cs2;
      clefDebug("exportbrf: clef segments cs1=%p cs2=%p cs3=%p seg=%p", cs1, cs2, cs3, seg);

      // output attribute at start of measure: clef
      if (seg) {
            for (int st = strack; st < etrack; st += VOICES) {
                  // sstaff - xml staff number, counting from 1 for this
                  // instrument
                  // special number 0 -> don’t show staff number in
                  // xml output (because there is only one staff)

                  int sstaff = (staves > 1) ? st - strack + VOICES : 0;
                  sstaff /= VOICES;

                  Clef* cle = static_cast<Clef*>(seg->element(st));
                  if (cle) {
                        clefDebug("exportbrf: clef at start measure ti=%d ct=%d gen=%d", tick, int(cle->clefType()), cle->generated());
                        // output only clef changes, not generated clefs at line beginning
                        // exception: at tick=0, export clef anyway
                        if (tick.isZero() || !cle->generated()) {
                              clefDebug("exportbrf: clef exported");
                              clef(sstaff, cle->clefType(), color2xml(cle));
                              }
                        else {
                              clefDebug("exportbrf: clef not exported");
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//  findPitchesUsed
//---------------------------------------------------------

/**
 Find the set of pitches actually used in a part.
 */

typedef QSet<int> pitchSet;       // the set of pitches used

static void addChordPitchesToSet(const Chord* c, pitchSet& set)
      {
      for (const Note* note : c->notes()) {
            qDebug("chord %p note %p pitch %d", c, note, note->pitch() + 1);
            set.insert(note->pitch());
            }
      }

static void findPitchesUsed(const Part* part, pitchSet& set)
      {
      int strack = part->startTrack();
      int etrack = part->endTrack();

      // loop over all chords in the part
      for (const MeasureBase* mb = part->score()->measures()->first(); mb; mb = mb->next()) {
            if (mb->type() != ElementType::MEASURE)
                  continue;
            const Measure* m = static_cast<const Measure*>(mb);
            for (int st = strack; st < etrack; ++st) {
                  for (Segment* seg = m->first(); seg; seg = seg->next()) {
                        const Element* el = seg->element(st);
                        if (!el)
                              continue;
                        if (el->type() == ElementType::CHORD)
                              {
                              // add grace and non-grace note pitches to the result set
                              const Chord* c = static_cast<const Chord*>(el);
                              if (c) {
                                    for (const Chord* g : c->graceNotesBefore()) {
                                          addChordPitchesToSet(g, set);
                                          }
                                    addChordPitchesToSet(c, set);
                                    for (const Chord* g : c->graceNotesAfter()) {
                                          addChordPitchesToSet(g, set);
                                          }
                                    }
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//  partList
//---------------------------------------------------------

/**
 Write the part list to \a brf.
 */

static void partList(BrfWriter& brf, Score* score, const QList<Part*>& il, MbrfInstrumentMap& instrMap)
      {
      brf.stag("part-list");
      int staffCount = 0;                             // count sum of # staves in parts
      int partGroupEnd[MAX_PART_GROUPS];              // staff where part group ends (bracketSpan is in staves, not parts)
      for (int i = 0; i < MAX_PART_GROUPS; i++)
            partGroupEnd[i] = -1;
      for (int idx = 0; idx < il.size(); ++idx) {
            Part* part = il.at(idx);
            bool bracketFound = false;
            // handle brackets
            for (int i = 0; i < part->nstaves(); i++) {
                  Staff* st = part->staff(i);
                  if (st) {
                        for (int j = 0; j < st->bracketLevels() + 1; j++) {
                              if (st->bracketType(j) != BracketType::NO_BRACKET) {
                                    bracketFound = true;
                                    if (i == 0) {
                                          // OK, found bracket in first staff of part
                                          // filter out implicit brackets
                                          if (!(st->bracketSpan(j) == part->nstaves()
                                                && st->bracketType(j) == BracketType::BRACE)) {
                                                // add others
                                                int number = findPartGroupNumber(partGroupEnd);
                                                if (number < MAX_PART_GROUPS) {
                                                      partGroupStart(brf, number + 1, st->bracketType(j));
                                                      partGroupEnd[number] = staffCount + st->bracketSpan(j);
                                                      }
                                                }
                                          }
                                    else {
                                          // bracket in other staff not supported in MusicXML
                                          qDebug("bracket starting in staff %d not supported", i + 1);
                                          }
                                    }
                              }
                        }
                  }
            // handle bracket none
            if (!bracketFound && part->nstaves() > 1) {
                  int number = findPartGroupNumber(partGroupEnd);
                  if (number < MAX_PART_GROUPS) {
                        partGroupStart(brf, number + 1, BracketType::NO_BRACKET);
                        partGroupEnd[number] = idx + part->nstaves();
                        }
                  }

            brf.stag(QString("score-part id=\"P%1\"").arg(idx+1));
            initInstrMap(instrMap, part->instruments(), score);
            // by default export the parts long name as part-name
            if (part->longName() != "")
                  brf.tag("part-name", MScoreTextToBraille::toPlainText(part->longName()));
            else {
                  if (part->partName() != "") {
                        // use the track name if no part long name
                        // to prevent an empty track name on import
                        brf.tag("part-name print-object=\"no\"", MScoreTextToBraille::toPlainText(part->partName()));
                        }
                  else
                        // part-name is required
                        brf.tag("part-name", "");
                  }
            if (!part->shortName().isEmpty())
                  brf.tag("part-abbreviation", MScoreTextToBraille::toPlainText(part->shortName()));

            if (part->instrument()->useDrumset()) {
                  const Drumset* drumset = part->instrument()->drumset();
                  pitchSet pitches;
                  findPitchesUsed(part, pitches);
                  for (int i = 0; i < 128; ++i) {
                        DrumInstrument di = drumset->drum(i);
                        if (di.notehead != NoteHead::Group::HEAD_INVALID)
                              scoreInstrument(brf, idx + 1, i + 1, di.name);
                        else if (pitches.contains(i))
                              scoreInstrument(brf, idx + 1, i + 1, QString("Instrument %1").arg(i + 1));
                        }
                  int midiPort = part->midiPort() + 1;
                  if (midiPort >= 1 && midiPort <= 16)
                        brf.tag(QString("midi-device port=\"%1\"").arg(midiPort), "");

                  for (int i = 0; i < 128; ++i) {
                        DrumInstrument di = drumset->drum(i);
                        if (di.notehead != NoteHead::Group::HEAD_INVALID || pitches.contains(i))
                              midiInstrument(brf, idx + 1, i + 1, part->instrument(), score, i + 1);
                        }
                  }
            else {
                  MbrfReverseInstrumentMap rim;
                  initReverseInstrMap(rim, instrMap);
                  for (int instNr : rim.keys()) {
                        scoreInstrument(brf, idx + 1, instNr + 1, MScoreTextToBraille::toPlainText(rim.value(instNr)->trackName()));
                        }
                  for (auto ii = rim.constBegin(); ii != rim.constEnd(); ii++) {
                        int instNr = ii.key();
                        int midiPort = part->midiPort() + 1;
                        if (ii.value()->channel().size() > 0)
                              midiPort = score->masterScore()->midiMapping(ii.value()->channel(0)->channel())->port() + 1;
                        if (midiPort >= 1 && midiPort <= 16)
                              brf.tag(QString("midi-device %1 port=\"%2\"").arg(instrId(idx+1, instNr + 1)).arg(midiPort), "");
                        else
                              brf.tag(QString("midi-device %1").arg(instrId(idx+1, instNr + 1)), "");
                        midiInstrument(brf, idx + 1, instNr + 1, rim.value(instNr), score);
                        }
                  }

            brf.etag();
            staffCount += part->nstaves();
            for (int i = MAX_PART_GROUPS - 1; i >= 0; i--) {
                  int end = partGroupEnd[i];
                  if (end >= 0) {
                        if (staffCount >= end) {
                              brf.tagE(QString("part-group type=\"stop\" number=\"%1\"").arg(i + 1));
                              partGroupEnd[i] = -1;
                              }
                        }
                  }
            }
      brf.etag();

      }

//---------------------------------------------------------
//  tickIsInMiddleOfMeasure
//---------------------------------------------------------

static bool tickIsInMiddleOfMeasure(const Fraction ti, const Measure* m)
      {
      return ti != m->tick() && ti != m->endTick();
      }

//---------------------------------------------------------
//  writeElement
//---------------------------------------------------------

/**
 Write \a el.
 */

void ExportBrf::writeElement(Element* el, const Measure* m, int sstaff, bool useDrumset)
      {
      if (el->isClef()) {
            // output only clef changes, not generated clefs
            // at line beginning
            // also ignore clefs at the start of a measure,
            // these have already been output
            // also ignore clefs at the end of a measure
            // these will be output at the start of the next measure
            const auto cle = toClef(el);
            const auto ti = cle->segment()->tick();
            clefDebug("exportbrf: clef in measure ti=%d ct=%d gen=%d", ti, int(cle->clefType()), el->generated());
            if (el->generated()) {
                  clefDebug("exportBrf: generated clef not exported");
                  }
            else if (!el->generated() && tickIsInMiddleOfMeasure(ti, m))
                  clef(sstaff, cle->clefType(), color2xml(cle));
            else
                  clefDebug("exportbrf: clef not exported");
            }
      else if (el->isChord()) {
            const auto c = toChord(el);
            const auto ll = &c->lyrics();
            // ise grace after
            if (c) {
                  for (const auto g : c->graceNotesBefore()) {
                        chord(g, sstaff, ll, useDrumset);
                        }
                  chord(c, sstaff, ll, useDrumset);
                  for (const auto g : c->graceNotesAfter()) {
                        chord(g, sstaff, ll, useDrumset);
                        }
                  }
            }
      else if (el->isRest()) {
            const auto r = toRest(el);
            if (!(r->isGap()))
                  rest(r, sstaff);
            }
      else if (el->isBarLine()) {
            const auto barln = toBarLine(el);
            if (tickIsInMiddleOfMeasure(barln->tick(), m))
                  barlineMiddle(barln);
            }
      else if (el->isKeySig() || el->isTimeSig() || el->isBreath()) {
            // handled elsewhere
            }
      else
            qDebug("ExportBrf::write unknown segment type %s", el->name());
      }

//---------------------------------------------------------
//  writeStaffDetails
//---------------------------------------------------------

/**
 Write the staff details for \a part to \a xml.
 */

static void writeStaffDetails(BrfWriter& brf, const Part* part)
      {
      const Instrument* instrument = part->instrument();
      int staves = part->nstaves();

      // staff details
      // TODO: decide how to handle linked regular / TAB staff
      //       currently exported as a two staff part ...
      for (int i = 0; i < staves; i++) {
            Staff* st = part->staff(i);
            if (st->lines(Fraction(0,1)) != 5 || st->isTabStaff(Fraction(0,1))) {
                  if (staves > 1)
                        brf.stag(QString("staff-details number=\"%1\"").arg(i+1));
                  else
                        brf.stag("staff-details");
                  brf.tag("staff-lines", st->lines(Fraction(0,1)));
                  if (st->isTabStaff(Fraction(0,1)) && instrument->stringData()) {
                        QList<instrString> l = instrument->stringData()->stringList();
                        for (int ii = 0; ii < l.size(); ii++) {
                              char step  = ' ';
                              int alter  = 0;
                              int octave = 0;
                              midipitch2xml(l.at(ii).pitch, step, alter, octave);
                              brf.stag(QString("staff-tuning line=\"%1\"").arg(ii+1));
                              brf.tag("tuning-step", QString("%1").arg(step));
                              if (alter)
                                    brf.tag("tuning-alter", alter);
                              brf.tag("tuning-octave", octave);
                              brf.etag();
                              }
                        }
                  brf.etag();
                  }
            }
      }

//---------------------------------------------------------
//  writeInstrumentDetails
//---------------------------------------------------------

/**
 Write the instrument details for \a part to \a xml.
 */

static void writeInstrumentDetails(BrfWriter& brf, const Part* part)
      {
      const Instrument* instrument = part->instrument();

      // instrument details
      if (instrument->transpose().chromatic) {        // TODO: tick
            brf.stag("transpose");
            brf.tag("diatonic",  instrument->transpose().diatonic % 7);
            brf.tag("chromatic", instrument->transpose().chromatic % 12);
            int octaveChange = instrument->transpose().chromatic / 12;
            if (octaveChange != 0)
                  brf.tag("octave-change", octaveChange);
            brf.etag();
            }
      }

//---------------------------------------------------------
//  annotationsWithoutNote
//---------------------------------------------------------

/**
 Write the annotations that could not be attached to notes.
 */

static void annotationsWithoutNote(ExportBrf* exp, const int strack, const int staves, const Measure* const measure)
      {
      for (auto segment = measure->first(); segment; segment = segment->next()) {
            if (segment->segmentType() == SegmentType::ChordRest) {
                  for (const auto element : segment->annotations()) {
                        if (!element->isFiguredBass() && !element->isHarmony()) {       // handled elsewhere
                              const auto wtrack = findTrackForAnnotations(element->track(), segment); // track to write annotation
                              if (strack <= element->track() && element->track() < (strack + VOICES * staves) && wtrack < 0) {
                                    exp->moveToTick(element->tick());
                                    commonAnnotations(exp, element, staves > 1 ? 1 : 0);
                                    }
                              }
                        }
                  }

            }
      }

//---------------------------------------------------------
//  MeasureNumberStateHandler
//---------------------------------------------------------

/**
 State handler used to calculate measure number including implict flag.
 To be called once at the start of each measure in a part.
 */

class MeasureNumberStateHandler final
      {
public:
      MeasureNumberStateHandler();
      void updateForMeasure(const Measure* const m);
      QString measureNumber() const;
      bool isFirstActualMeasure() const;
private:
      void init();
      int _measureNo;                     // number of next regular measure
      int _irregularMeasureNo;            // number of next irregular measure
      int _pickupMeasureNo;               // number of next pickup measure
      QString _cachedAttributes;          // attributes calculated by updateForMeasure()
      };

MeasureNumberStateHandler::MeasureNumberStateHandler()
      {
      init();
      }

void MeasureNumberStateHandler::init()
      {
      _measureNo = 1;
      _irregularMeasureNo = 1;
      _pickupMeasureNo = 1;
      }


void MeasureNumberStateHandler::updateForMeasure(const Measure* const m)
      {
      // restart measure numbering after a section break if startWithMeasureOne is set
      const auto previousMeasure = m->prevMeasure();
      if (previousMeasure) {
            const auto layoutSectionBreak = previousMeasure->sectionBreakElement();
            if (layoutSectionBreak && layoutSectionBreak->startWithMeasureOne())
                  init();
            }

      // update measure numers and cache result
      _cachedAttributes = " number=";
      if ((_irregularMeasureNo + _measureNo) == 2 && m->irregular()) {
            _cachedAttributes += "\"0\" implicit=\"yes\"";
            _pickupMeasureNo++;
            }
      else if (m->irregular())
            _cachedAttributes += QString("\"X%1\" implicit=\"yes\"").arg(_irregularMeasureNo++);
      else
            _cachedAttributes += QString("\"%1\"").arg(_measureNo++);
      }

QString MeasureNumberStateHandler::measureNumber() const
      {
      return _cachedAttributes;
      }

bool MeasureNumberStateHandler::isFirstActualMeasure() const
      {
      return (_irregularMeasureNo + _measureNo + _pickupMeasureNo) == 4;
      }

//---------------------------------------------------------
//  write
//---------------------------------------------------------

/**
 Write the score to \a dev in Brf format.
 */

void ExportBrf::write(QIODevice* dev)
      {
      // consider allowing concert pitch export
      // must export in transposed pitch to prevent
      // losing the transposition information
      // if necessary, switch concert pitch mode off
      // before export and restore it after export
      bool concertPitch = score()->styleB(Sid::concertPitch);
      if (concertPitch) {
            score()->startCmd();
            score()->undo(new ChangeStyleVal(score(), Sid::concertPitch, false));
            score()->doLayout();    // this is only allowed in a cmd context to not corrupt the undo/redo stack
            }

      calcDivisions();

      for (int i = 0; i < MAX_NUMBER_LEVEL; ++i) {
            brackets[i] = nullptr;
            dashes[i] = nullptr;
            hairpins[i] = nullptr;
            ottavas[i] = nullptr;
            trills[i] = nullptr;
            }

      _brf.setDevice(dev);
      // _brf.setCodec("UTF-8");
      // _brf << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
      // _brf << "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 3.1 Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">\n";
      // _brf.stag("score-partwise version=\"3.1\"");

      work(_score->measures()->first());

      identification(_brf, _score);

      if (preferences.getBool(PREF_EXPORT_BRAILLE_EXPORTLAYOUT)) {
            defaults(_brf, _score, millimeters, tenths);
            credits(_brf);
            }

      const auto& il = _score->parts();
      partList(_brf, _score, il, instrMap);

      int staffCount = 0;

      for (int idx = 0; idx < il.size(); ++idx) {
            const auto part = il.at(idx);
            _tick = { 0,1 };
            _brf.stag(QString("part id=\"P%1\"").arg(idx+1));

            int staves = part->nstaves();
            int strack = part->startTrack();
            int etrack = part->endTrack();

            _trillStart.clear();
            _trillStop.clear();
            initInstrMap(instrMap, part->instruments(), _score);

            MeasureNumberStateHandler mnsh;

            FigBassMap fbMap;           // pending figured bass extends

            for (auto mb = _score->measures()->first(); mb; mb = mb->next()) {
                  if (!mb->isMeasure())
                        continue;
                  const auto m = toMeasure(mb);

                  // pickup and other irregular measures need special care
                  QString measureTag = "measure";
                  mnsh.updateForMeasure(m);
                  measureTag += mnsh.measureNumber();
                  const bool isFirstActualMeasure = mnsh.isFirstActualMeasure();

                  if (preferences.getBool(PREF_EXPORT_BRAILLE_EXPORTLAYOUT))
                        measureTag += QString(" width=\"%1\"").arg(QString::number(m->bbox().width() / DPMM / millimeters * tenths,'f',2));

                  _brf.stag(measureTag);

                  print(m, idx, staffCount, staves);

                  _attr.start();

                  findTrills(m, strack, etrack, _trillStart, _trillStop);

                  // barline left must be the first element in a measure
                  barlineLeft(m);

                  // output attributes with the first actual measure (pickup or regular)
                  if (isFirstActualMeasure) {
                        _attr.doAttr(_brf, true);
                        _brf.tag("divisions", MScore::division / div);
                        }

                  // output attributes at start of measure: key, time
                  keysigTimesig(m, part);

                  // output attributes with the first actual measure (pickup or regular) only
                  if (isFirstActualMeasure) {
                        if (staves > 1)
                              _brf.tag("staves", staves);
                        if (instrMap.size() > 1)
                              _brf.tag("instruments", instrMap.size());
                        }

                  // make sure clefs at end of measure get exported at start of next measure
                  findAndExportClef(m, staves, strack, etrack);

                  // make sure a clef gets exported if none is found
                  exportDefaultClef(part, m);

                  // output attributes with the first actual measure (pickup or regular) only
                  if (isFirstActualMeasure) {
                        writeStaffDetails(_brf, part);
                        writeInstrumentDetails(_brf, part);
                        }

                  // output attribute at start of measure: measure-style
                  measureStyle(_brf, _attr, m);

                  // set of spanners already stopped in this measure
                  // required to prevent multiple spanner stops for the same spanner
                  QSet<const Spanner*> spannersStopped;

                  // MuseScore limitation: repeats are always in the first part
                  // and are implicitly placed at either measure start or stop
                  if (idx == 0)
                        repeatAtMeasureStart(_brf, _attr, m, strack, etrack, strack);

                  for (int st = strack; st < etrack; ++st) {
                        // sstaff - xml staff number, counting from 1 for this
                        // instrument
                        // special number 0 -> don’t show staff number in
                        // xml output (because there is only one staff)

                        int sstaff = (staves > 1) ? st - strack + VOICES : 0;
                        sstaff /= VOICES;
                        for (auto seg = m->first(); seg; seg = seg->next()) {
                              const auto el = seg->element(st);
                              if (!el) {
                                    continue;
                                    }
                              // must ignore start repeat to prevent spurious backup/forward
                              if (el->isBarLine() && toBarLine(el)->barLineType() == BarLineType::START_REPEAT)
                                    continue;

                              // generate backup or forward to the start time of the element
                              if (_tick != seg->tick()) {
                                    _attr.doAttr(_brf, false);
                                    moveToTick(seg->tick());
                                    }

                              // handle annotations and spanners (directions attached to this note or rest)
                              if (el->isChordRest()) {
                                    _attr.doAttr(_brf, false);
                                    annotations(this, strack, etrack, st, sstaff, seg);
                                    // look for more harmony
                                    for (auto seg1 = seg->next(); seg1; seg1 = seg1->next()) {
                                          if (seg1->isChordRestType()) {
                                                const auto el1 = seg1->element(st);
                                                if (el1) // found a ChordRest, next harmony will be attach to this one
                                                      break;
                                                for (auto annot : seg1->annotations()) {
                                                      if (annot->isHarmony() && annot->track() == st)
                                                            harmony(toHarmony(annot), 0, (seg1->tick() - seg->tick()).ticks() / div);
                                                      }
                                                }
                                          }
                                    figuredBass(_brf, strack, etrack, st, static_cast<const ChordRest*>(el), fbMap, div);
                                    spannerStart(this, strack, etrack, st, sstaff, seg);
                                    }

                              // write element el if necessary
                              writeElement(el, m, sstaff, part->instrument()->useDrumset());

                              // handle annotations and spanners (directions attached to this note or rest)
                              if (el->isChordRest()) {
                                    int spannerStaff = (st / VOICES) * VOICES;
                                    spannerStop(this, spannerStaff, _tick, sstaff, spannersStopped);
                                    }

                              } // for (Segment* seg = ...
                        _attr.stop(_brf);
                        } // for (int st = ...

                  // write the annotations that could not be attached to notes
                  annotationsWithoutNote(this, strack, staves, m);

                  // move to end of measure (in case of incomplete last voice)
#ifdef DEBUG_TICK
                  qDebug("end of measure");
#endif
                  moveToTick(m->endTick());
                  if (idx == 0)
                        repeatAtMeasureStop(_brf, m, strack, etrack, strack);
                  // note: don't use "m->repeatFlags() & Repeat::END" here, because more
                  // barline types need to be handled besides repeat end ("light-heavy")
                  barlineRight(m);
                  _brf.etag();
                  }
            staffCount += staves;
            _brf.etag();
            }

      _brf.etag();

      if (concertPitch) {
            // restore concert pitch
            score()->endCmd(true);        // rollback
            }
      }

//---------------------------------------------------------
//   saveBrf
//    return false on error
//---------------------------------------------------------

/**
 Save Score as brf file \a name.

 Return false on error.
 */

bool saveBrf(Score* score, QIODevice* device)
      {
      ExportBrf em(score);
      em.write(device);
      return true;
      }

bool saveBrf(Score* score, const QString& name)
      {
      QFile f(name);
      if (!f.open(QIODevice::WriteOnly))
            return false;

      bool res = saveBrf(score, &f) && (f.error() == QFile::NoError);
      f.close();
      return res;
      }


double ExportBrf::getTenthsFromInches(double inches) const
      {
      return inches * INCH / millimeters * tenths;
      }

double ExportBrf::getTenthsFromDots(double dots) const
      {
      return dots / DPMM / millimeters * tenths;
      }

//---------------------------------------------------------
//   harmony
//---------------------------------------------------------

void ExportBrf::harmony(Harmony const* const h, FretDiagram const* const fd, int offset)
      {
      // this code was probably in place to allow chord symbols shifted *right* to export with offset
      // since this was at once time the only way to get a chord to appear over beat 3 in an empty 4/4 measure
      // but the value was calculated incorrectly (should be divided by spatium) and would be better off using offset anyhow
      // since we now support placement of chord symbols over "empty" beats directly,
      // and we don't generally export position info for other elements
      // it's just as well to not bother doing so here
      //double rx = h->offset().x()*10;
      //QString relative;
      //if (rx > 0) {
      //      relative = QString(" relative-x=\"%1\"").arg(QString::number(rx,'f',2));
      //      }
      int rootTpc = h->rootTpc();
      if (rootTpc != Tpc::TPC_INVALID) {
            QString tagName = "harmony";
            bool frame = h->hasFrame();
            tagName += QString(" print-frame=\"%1\"").arg(frame ? "yes" : "no"); // .append(relative));
            tagName += color2xml(h);
            _brf.stag(tagName);
            _brf.stag("root");
            _brf.tag("root-step", tpc2stepName(rootTpc));
            int alter = int(tpc2alter(rootTpc));
            if (alter)
                  _brf.tag("root-alter", alter);
            _brf.etag();

            if (!h->xmlKind().isEmpty()) {
                  QString s = "kind";
                  QString kindText = h->musicXmlText();
                  if (h->musicXmlText() != "")
                        s += " text=\"" + kindText + "\"";
                  if (h->xmlSymbols() == "yes")
                        s += " use-symbols=\"yes\"";
                  if (h->xmlParens() == "yes")
                        s += " parentheses-degrees=\"yes\"";
                  _brf.tag(s, h->xmlKind());
                  QStringList l = h->xmlDegrees();
                  if (!l.isEmpty()) {
                        for (QString tag : l) {
                              QString degreeText;
                              if (h->xmlKind().startsWith("suspended")
                                  && tag.startsWith("add") && tag[3].isDigit()
                                  && !kindText.isEmpty() && kindText[0].isDigit()) {
                                    // hack to correct text for suspended chords whose kind text has degree information baked in
                                    // (required by some other applications)
                                    int tagDegree = tag.mid(3).toInt();
                                    QString kindTextExtension;
                                    for (int i = 0; i < kindText.length() && kindText[i].isDigit(); ++i)
                                          kindTextExtension[i] = kindText[i];
                                    int kindExtension = kindTextExtension.toInt();
                                    if (tagDegree <= kindExtension && (tagDegree & 1) && (kindExtension & 1))
                                          degreeText = " text=\"\"";
                                    }
                              _brf.stag("degree");
                              alter = 0;
                              int idx = 3;
                              if (tag[idx] == '#') {
                                    alter = 1;
                                    ++idx;
                                    }
                              else if (tag[idx] == 'b') {
                                    alter = -1;
                                    ++idx;
                                    }
                              _brf.tag(QString("degree-value%1").arg(degreeText), tag.mid(idx));
                              _brf.tag("degree-alter", alter);     // finale insists on this even if 0
                              if (tag.startsWith("add"))
                                    _brf.tag(QString("degree-type%1").arg(degreeText), "add");
                              else if (tag.startsWith("sub"))
                                    _brf.tag("degree-type", "subtract");
                              else if (tag.startsWith("alt"))
                                    _brf.tag("degree-type", "alter");
                              _brf.etag();
                              }
                        }
                  }
            else {
                  if (h->extensionName() == 0)
                        _brf.tag("kind", "");
                  else
                        _brf.tag(QString("kind text=\"%1\"").arg(h->extensionName()), "");
                  }

            int baseTpc = h->baseTpc();
            if (baseTpc != Tpc::TPC_INVALID) {
                  _brf.stag("bass");
                  _brf.tag("bass-step", tpc2stepName(baseTpc));
                  alter = int(tpc2alter(baseTpc));
                  if (alter) {
                        _brf.tag("bass-alter", alter);
                        }
                  _brf.etag();
                  }
            if (offset > 0)
                  _brf.tag("offset", offset);
            if (fd)
                  fd->writeMusicXML(_brf);

            _brf.etag();
            }
      else {
            //
            // export an unrecognized Chord
            // which may contain arbitrary text
            //
            if (h->hasFrame())
                  _brf.stag(QString("harmony print-frame=\"yes\""));     // .append(relative));
            else
                  _brf.stag(QString("harmony print-frame=\"no\""));      // .append(relative));
            switch (h->harmonyType()) {
                  case HarmonyType::NASHVILLE: {
                        _brf.tag("function", h->hFunction());
                        QString k = "kind text=\"" + h->hTextName() + "\"";
                        _brf.tag(k, "none");
                        }
                        break;
                  case HarmonyType::ROMAN: {
                        // TODO: parse?
                        _brf.tag("function", h->hTextName());
                        QString k = "kind text=\"\"";
                        _brf.tag(k, "none");
                        }
                        break;
                  case HarmonyType::STANDARD:
                  default: {
                        _brf.stag("root");
                        _brf.tag("root-step text=\"\"", "C");
                        _brf.etag();       // root
                        QString k = "kind text=\"" + h->hTextName() + "\"";
                        _brf.tag(k, "none");
                        }
                        break;
                  }
            _brf.etag();       // harmony

            }

      }

} // Ms