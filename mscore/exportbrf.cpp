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


#include <math.h>
#include "config.h"
#include "musescore.h"
#include "file.h"
#include "libmscore/score.h"
#include "libmscore/rest.h"
#include "libmscore/chord.h"
#include "libmscore/sig.h"
#include "libmscore/key.h"
#include "libmscore/clef.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/xml.h"
#include "libmscore/brf.h"
#include "libmscore/beam.h"
#include "libmscore/staff.h"
#include "libmscore/part.h"
#include "libmscore/measure.h"
#include "libmscore/style.h"
#include "musicxml.h"           //will delete this line when file replaced
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
#include "musicxmlfonthandler.h"
#include "braillecellmanager.h"

namespace Ms {

//---------------------------------------------------------
//   ExportBraille
//---------------------------------------------------------


class ExportBraille {
      Score* _score;
      BrfWriter _brf;
      //SlurHandler sh;
      //GlissandoHandler gh;
      Fraction _tick;
      //Attributes _attr;
      TextLine const* brackets[MAX_NUMBER_LEVEL];
      TextLineBase const* dashes[MAX_NUMBER_LEVEL];
      Hairpin const* hairpins[MAX_NUMBER_LEVEL];
      Ottava const* ottavas[MAX_NUMBER_LEVEL];
      Trill const* trills[MAX_NUMBER_LEVEL];
      int div;
      double millimeters;
      int tenths;
      //TrillHash _trillStart;
      //TrillHash _trillStop;
      //MxmlInstrumentMap instrMap;

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
      ExportBraille(Score* s)
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
      }; // class ExportBraille

//---------------------------------------------------------
//   saveBrf
//    return false on error
//---------------------------------------------------------

/**
 Save Score as Braille file \a name.

 Return false on error.
 */

bool saveBrf(Score* score, QIODevice* device)
      {
      ExportBraille em(score);
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

} // namespace Ms
