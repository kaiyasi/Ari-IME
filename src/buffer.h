// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#ifndef INPUTER_BUFFER_H
#define INPUTER_BUFFER_H

#include <string>
#include <vector>

#include <fcitx-utils/key.h>

#include "layout.h"
#include "zhuyin.h"

// Result of feeding one key into the state machine, applied by the engine.
struct KeyResult {
    bool handled = false;      // If false, let the application handle the key.
    bool hasCommit = false;    // commitText should be committed to the client.
    std::string commitText;
    bool updateUI = false;     // Engine should refresh pre-edit + candidates.
    bool notifyMode = false;   // Engine should pop a transient 中/英 mode hint.
};

// Per-input-context state machine implementing ASUS-style simultaneous mixed
// input WITHOUT mode switching.
//
// English-first model: every raw key shows as itself in the pre-edit. Keys are
// only turned into Chinese once they actually form a complete, toned 注音
// syllable (e.g. "su" stays "su" until a tone arrives: "su3" -> 你). Nothing is
// ever auto-broken or committed mid-stream — the whole pre-edit (which may freely
// mix English and Chinese in any order, e.g. "acer螢幕") is held until the user
// presses Enter, the only key that commits to the application. Space is a 一聲
// for a pending bopomofo syllable, otherwise a literal space; it ends the current
// token but, like everything else, does not commit.
//
// To disambiguate the two regimes the state machine still tracks whether the
// current token is Chinese (greedily growing one syllable; a class collision
// flips it to English) or English (letters accumulate; only a tone peels the
// shortest trailing syllable off the tail, keeping brand names like "acer"
// intact).
//
// Layout of the pre-edit: a list of finalized cells_ (each one character, and
// each Chinese cell remembers its 注音 reading) followed by the live tail — the
// chewing run, then englishBuf_, then the in-progress raw syllable syl_. The
// live run gives chewing phrasing while contiguous Chinese is being typed; the
// moment English breaks it (or the user opens selection) the run is frozen into
// cells_. Because every Chinese cell keeps its reading, candidate re-selection
// works on ANY character in the string regardless of surrounding English: Down
// enters a selection mode whose cursor moves over the whole pre-edit.
//
// Ctrl+Space additionally toggles a forced pure-English mode (no 注音) that
// persists until toggled again; text is still held in the pre-edit until Enter.
class Buffer {
public:
    KeyResult handleKey(const fcitx::Key &key);
    // Clear composition/candidates/caret state. User mode toggles such as
    // forced English persist until the user toggles them explicitly.
    void reset();

    // Insert clipboard / external text at the caret (Ctrl+V): drops in as literal
    // cells and leaves the caret right after them, so editing continues there.
    void pasteAtCaret(const std::string &text);

    std::string preeditText() const;
    std::vector<std::string> candidates() const;
    // Select a candidate on the currently visible page. Used by UI candidate
    // activation (mouse/touch) so it shares the same path as number-key picking.
    KeyResult selectCandidate(int pageIndex);
    int candidatePage() const;
    int candidatePageCount() const;
    // Index (within the current candidate page) of the highlighted candidate,
    // or -1 when no candidate window is open.
    int highlight() const;
    // Character index in the pre-edit currently being selected, or -1 when no
    // candidate window is open. Lets the UI highlight just that one character.
    int selectionChar() const;
    // Character index where the pre-edit caret belongs, or -1 for the very end.
    // Tracks the selected cell while selecting and the insertion point while
    // typing mid-string, so the caret never jumps to the end during insertion.
    int caretChar() const;

    // --- Configuration (driven by the fcitx5 addon config) ---
    // When on, non-注音 punctuation keys produce chewing's full-width Chinese
    // punctuation (e.g. '<' -> ，, '?' -> ？); off (default) keeps them literal.
    bool setFullWidthPunct(bool on) {
        if (fullWidthPunct_ == on) {
            return false;
        }
        fullWidthPunct_ = on;
        return true;
    }
    bool setKeyboardLayout(inputer::KeyboardLayout layout);

    // Forced pure-English mode (toggled by Ctrl+Space); lets the engine show the
    // current 中/英 mode hint.
    bool isForcedEnglish() const { return forcedEnglish_; }
    bool isFullWidthPunct() const { return fullWidthPunct_; }
    inputer::KeyboardLayout keyboardLayout() const { return layout_; }

private:
    enum class Token { Chinese, English };

    // One display unit of the pre-edit. Chinese cells remember the 注音 reading
    // that produced them, so any character — wherever it sits in the string and
    // whatever English surrounds it — can later be re-opened for candidate
    // re-selection. English cells hold one literal character (reading unused).
    struct Cell {
        bool chinese = false;
        std::string text;    // the displayed character (one codepoint)
        std::string reading; // canonical 注音 keys; trailing ' ' marks 一聲
        bool locked = false; // the user explicitly picked this character; pin it
                             // (as a single) whenever the run is re-fed, so it
                             // survives later picks elsewhere in the run.
    };

    // --- Typing path ---
    KeyResult handleAuto(const fcitx::Key &key);
    // `literal` keys (e.g. numeric-keypad digits) are never interpreted as 注音;
    // they break any pending syllable and go straight into the English tail.
    KeyResult handleChar(char c, bool literal = false);
    // Feed a complete, canonicalised syllable body into the live chewing run,
    // extending it so chewing's phrasing spans contiguous Chinese. English in
    // front of it freezes the old run first, preserving order.
    void integrateSyllable(const std::string &body);
    // chewing's auto-conversion can differ from the top of its own candidate list
    // (e.g. it auto-picks 妳好 while the selection window ranks 你好 first). Greedily
    // re-pick candidate[0] of the longest interval left-to-right so the displayed
    // text always equals what selection would offer first ("以選字候選為準"). Leaves
    // the edit cursor at the end so live typing keeps appending.
    void normalizeRunToTop();
    // Abandon the current 注音 hypothesis WITHOUT committing: the in-progress
    // syllable plus `trailing` become the live English tail after the run.
    KeyResult flipToEnglish(char trailing);
    // While in an English token, a tone key may complete a trailing 注音
    // syllable (e.g. "acer" + "u/6" -> acer螢). Peel it off when the remaining
    // prefix is empty or a real English word, so English typing isn't hijacked.
    bool tryPeelEnglish(char tone, KeyResult &out);
    KeyResult handleSpace();
    KeyResult handleEnter();
    KeyResult handleBackspace();
    KeyResult handleDelete();
    // On commit, replay each Chinese run into chewing with the user's chosen
    // characters selected and commit it, so chewing's autoLearn records the
    // phrase/homophone frequencies (personal adaptation over time).
    void learnFromCells();

    // --- Freezing the live tail (run / English / syllable) into cells_ ---
    void freezeRun();        // live chewing run -> Chinese cells (with readings)
    void freezeEnglish();    // englishBuf_ -> English cells
    void freezeSyllable();   // raw syl_ -> English cells
    void freezeAll();        // all three, in order
    void moveAutoCommit();   // pull chewing's auto-committed front chars into cells_

    // One re-selection candidate. `down` and `idx` say how to re-pick it from a
    // freshly-fed run: press Down `down` times to reach its interval (0 = the
    // longest phrase interval), then choose global index `idx` within it.
    struct SelCand {
        std::string text;
        std::string display;
        int down = 0;
        int idx = 0;
    };

    // --- Editing mode: a caret over cells_ with two sub-states ---
    // Caret mode (candOpen_ == false): the caret sits BETWEEN characters; arrows
    // move it, printable keys (digits included) insert as ordinary input, ↓/↑
    // open the candidate window to re-pick the character right of the caret
    // (or the last character when the caret is at the end).
    // Picking mode (candOpen_ == true): the classic candidate window over one
    // cell; ↑↓ navigate, number keys pick, Esc returns to caret mode.
    KeyResult enterSelection(const fcitx::Key &key); // freeze + enter caret mode
    KeyResult handleSelecting(const fcitx::Key &key);
    KeyResult handleCaret(const fcitx::Key &key);    // caret mode dispatch
    KeyResult handlePicking(const fcitx::Key &key);  // candidate-window dispatch
    // Open the candidate window to re-pick the cell at `cell` (or, for an English
    // cell on ↑, reinterpret it back into 注音). Enters picking mode.
    KeyResult openCandidatesAt(int cell, bool reinterpret);
    void loadCellCandidates();              // build phrase+single candidates here
    // Feed the contiguous Chinese run [start..end] into chewing and park the
    // cursor on character `offset`. Returns the run length.
    int feedRun(int start, int end, int offset);
    // After a fresh feed, chewing shows its own default conversion. Re-pick the
    // cells' chosen text as single-character choices so the user's earlier picks
    // are restored and locked. With onlyLocked the pass touches just the cells the
    // user explicitly picked (used when re-opening selection, so other characters
    // keep their phrase candidates); otherwise it forces every differing cell
    // (used before commit so autoLearn records exactly the displayed text).
    void relockRun(int start, int end, bool onlyLocked);
    // The maximal contiguous Chinese run of cells containing `idx`.
    void chineseRunAround(int idx, int &start, int &end) const;
    // Choose chewing candidate `globalIdx` (chewing's choose is page-relative, so
    // page there first). Assumes the candidate window is at the right interval.
    void chooseGlobalCandidate(int globalIdx);
    // Collect chewing's candidates at the cursor — longest phrase interval first,
    // down to single characters — into selCands_.
    void buildSelCands();
    int visibleCandidateCount() const;
    // Write chewing's current buffer back over the run's cell texts.
    void applyRunToCells();
    KeyResult moveSelCursor(int delta);     // step to the adjacent cell
    KeyResult pickCandidate(int pageIndex); // pick a candidate on the current page
    // Type while selecting: leave selection and resume the NORMAL typing path
    // right at the cursor cell. Everything from the cursor onward is parked in
    // tail_ so the keystroke (and whatever follows) composes exactly as it would
    // at the end of the line — 注音 auto-splitting, candidates, English — only
    // the result lands before the parked tail, which reconnects on commit /
    // re-selection. This is the mid-string insertion point. `pos` is the cell
    // index to insert before.
    KeyResult beginInsert(int pos, const fcitx::Key &key);
    // Fold the live typing tail into cells_, then re-append the parked tail_,
    // reuniting the pre-edit into one cells_ list (used before commit and before
    // re-entering selection).
    void mergeTail();
    // ↑ on an English cell: gather this cell plus the next few cells' raw keys
    // and, if they form a 注音 syllable, merge them into one Chinese cell and
    // open its candidates — recovering "catsu3" -> cat + 你 when the syllable's
    // 聲母 was wrongly absorbed into the English run.
    KeyResult reinterpretFromCell();
    // ↑ on a Chinese cell: explode it back into its raw 注音 keys as English
    // cells (你 -> s u 3), for when the literal keys were what was wanted.
    KeyResult revertCellToEnglish();
    void exitSelection();

    bool forcedEnglish_ = false;
    bool fullWidthPunct_ = false;
    inputer::KeyboardLayout layout_ = inputer::KeyboardLayout::Default;
    Token token_ = Token::Chinese;
    std::vector<Cell> cells_;             // finalized pre-edit, before the live tail
    std::vector<Cell> tail_;              // cells parked AFTER the live tail while
                                          // inserting mid-string; empty otherwise
    std::vector<std::string> runReadings_; // readings parallel to the live run chars
    std::string englishBuf_;             // live English tail, after the chewing run
    std::string syl_;                    // raw keys of the in-progress 注音 syllable
    bool selecting_ = false;             // editing mode active (caret or picking)
    bool candOpen_ = false;              // picking mode: candidate window is open
    int caretPos_ = 0;                   // caret BETWEEN cells, range [0, size]
    int selCursor_ = 0;                  // index into cells_ being re-selected
    int selRunStart_ = 0;                // contiguous Chinese run under the cursor
    int selRunEnd_ = 0;                  // (inclusive)
    bool runLoaded_ = false;             // zhuyin_ holds [start..end] with its locks
    std::vector<SelCand> selCands_;      // merged phrase+single candidate list
    int selPage_ = 0;                    // page (of 9) within selCands_
    int highlight_ = 0;                  // highlighted candidate within the page
    Zhuyin zhuyin_;                      // live Chinese run; scratch while selecting
};

#endif // INPUTER_BUFFER_H
