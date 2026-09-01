#include "filemgr.h"
#include "../storage/littlefs_ops.h"
#include "../ui/display.h"
#include "../ui/keys.h"
#include "../audio/sfx.h"
#include "../piglet/avatar.h"
#include "../core/config.h"
#include "../core/app.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <LittleFS.h>
#include <string.h>
#include <stdio.h>

bool FileMgrMode::running = false;
FileMgrMode::Phase FileMgrMode::phase = FileMgrMode::Phase::BROWSE;
FileMgrMode::Volume FileMgrMode::vol = FileMgrMode::Volume::SD;
char FileMgrMode::curPath[PATH_BUF] = "/";
FileMgrMode::Entry FileMgrMode::entries[MAX_ENTRIES] = {};
uint8_t FileMgrMode::entryCount = 0;
uint8_t FileMgrMode::sel = 0;
uint8_t FileMgrMode::scroll = 0;
bool FileMgrMode::keyLatch = false;
char FileMgrMode::statusMsg[40] = "";
char FileMgrMode::buf[EDIT_CAP] = {0};
uint16_t FileMgrMode::bufLen = 0;
uint16_t FileMgrMode::cursor = 0;
bool FileMgrMode::dirty = false;
char FileMgrMode::openName[40] = "";
uint16_t FileMgrMode::viewTopLine = 0;

// Resolve the active volume to its Arduino FS object. Takes a bool rather
// than FileMgrMode::Volume since Volume is private to the class.
static inline fs::FS& fsFor(bool isSd) {
    return isSd ? (fs::FS&)SD : (fs::FS&)LittleFS;
}

void FileMgrMode::buildFullPath(char* out, size_t n, const char* name) {
    if (strcmp(curPath, "/") == 0) snprintf(out, n, "/%s", name);
    else snprintf(out, n, "%s/%s", curPath, name);
}

void FileMgrMode::refreshList() {
    entryCount = 0;
    sel = 0;
    scroll = 0;
    bool isSd = (vol == Volume::SD);
    if (isSd && !Config::isSDAvailable()) return;
    fs::FS& fsr = fsFor(isSd);
    File d = fsr.open(curPath);
    if (!d || !d.isDirectory()) { if (d) d.close(); return; }
    File e = d.openNextFile();
    while (e && entryCount < MAX_ENTRIES) {
        const char* nm = e.name();
        const char* base = strrchr(nm, '/');
        base = base ? base + 1 : nm;
        if (base[0] != '\0') {
            Entry& ent = entries[entryCount];
            strncpy(ent.name, base, sizeof(ent.name) - 1);
            ent.name[sizeof(ent.name) - 1] = '\0';
            ent.isDir = e.isDirectory();
            ent.size = ent.isDir ? 0 : (uint32_t)e.size();
            entryCount++;
        }
        e.close();
        e = d.openNextFile();
    }
    d.close();
    // dirs first, then alphabetical — simple insertion sort, table is tiny
    for (uint8_t i = 1; i < entryCount; i++) {
        Entry tmp = entries[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 &&
               ((entries[j].isDir ? 0 : 1) > (tmp.isDir ? 0 : 1) ||
                ((entries[j].isDir == tmp.isDir) && strcasecmp(entries[j].name, tmp.name) > 0))) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = tmp;
    }
}

void FileMgrMode::enterDir(const char* name) {
    char next[PATH_BUF];
    buildFullPath(next, sizeof(next), name);
    strncpy(curPath, next, sizeof(curPath) - 1);
    curPath[sizeof(curPath) - 1] = '\0';
    refreshList();
}

void FileMgrMode::goUp() {
    if (strcmp(curPath, "/") == 0) return;
    char* slash = strrchr(curPath, '/');
    if (slash == curPath) curPath[1] = '\0';
    else if (slash) *slash = '\0';
    refreshList();
}

bool FileMgrMode::loadFile(const char* path) {
    fs::FS& fsr = fsFor(vol == Volume::SD);
    File f = fsr.open(path, FILE_READ);
    if (!f || f.isDirectory()) { if (f) f.close(); return false; }
    size_t sz = f.size();
    if (sz > EDIT_CAP - 1) sz = EDIT_CAP - 1;
    bufLen = (uint16_t)f.read((uint8_t*)buf, sz);
    buf[bufLen] = '\0';
    f.close();
    cursor = 0;
    viewTopLine = 0;
    dirty = false;
    return true;
}

bool FileMgrMode::saveFile() {
    char path[PATH_BUF];
    buildFullPath(path, sizeof(path), openName);
    fs::FS& fsr = fsFor(vol == Volume::SD);
    File f = fsr.open(path, FILE_WRITE);
    if (!f) return false;
    f.write((const uint8_t*)buf, bufLen);
    f.close();
    dirty = false;
    return true;
}

bool FileMgrMode::openSelected() {
    if (entryCount == 0) return false;
    Entry& e = entries[sel];
    if (e.isDir) { enterDir(e.name); return false; }

    // txt-ish only — anything else we just show size/name, no binary editing
    size_t L = strlen(e.name);
    bool looksText = (L >= 4 && strcasecmp(e.name + L - 4, ".txt") == 0) ||
                      (L >= 4 && strcasecmp(e.name + L - 4, ".log") == 0) ||
                      (L >= 4 && strcasecmp(e.name + L - 4, ".csv") == 0) ||
                      (L >= 4 && strcasecmp(e.name + L - 4, ".ini") == 0) ||
                      (L >= 3 && strcasecmp(e.name + L - 3, ".md") == 0);
    if (!looksText) {
        snprintf(statusMsg, sizeof(statusMsg), "%s  %luB", e.name, (unsigned long)e.size);
        return false;
    }
    char path[PATH_BUF];
    buildFullPath(path, sizeof(path), e.name);
    if (!loadFile(path)) {
        strncpy(statusMsg, "OPEN FAIL", sizeof(statusMsg) - 1);
        return false;
    }
    strncpy(openName, e.name, sizeof(openName) - 1);
    openName[sizeof(openName) - 1] = '\0';
    phase = Phase::VIEW;
    return true;
}

void FileMgrMode::start() {
    running = true;
    phase = Phase::BROWSE;
    vol = Volume::SD;
    strncpy(curPath, "/", sizeof(curPath) - 1);
    keyLatch = true;
    strncpy(statusMsg, "SD", sizeof(statusMsg) - 1);
    refreshList();
    SFX::setMuted(false);
    SFX::stop();
    SFX::play(SFX::MODE_ENTER);
    Avatar::setState(AvatarState::HUNTING);
}

void FileMgrMode::stop() {
    running = false;
    SFX::setMuted(false);
    SFX::stop();
    Avatar::setState(AvatarState::NEUTRAL);
}

void FileMgrMode::handleBrowseInput() {
    if (!keyNewPress(keyLatch)) return;

    if (keyEsc()) { stop(); return; }

    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
        if (sel > 0) sel--;
        if (sel < scroll) scroll = sel;
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('.')) {
        if (entryCount && sel + 1 < entryCount) sel++;
        if (sel >= scroll + VIS_ROWS) scroll = (uint8_t)(sel - VIS_ROWS + 1);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(',')) {  // left = up a dir
        goUp();
        SFX::play(SFX::BACK_NAV);
        return;
    }
    if (M5Cardputer.Keyboard.keysState().enter ||
        M5Cardputer.Keyboard.isKeyPressed('/')) {  // enter/right = open
        if (openSelected()) SFX::play(SFX::CONFIRM);
        else SFX::playNav();
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('v') || M5Cardputer.Keyboard.isKeyPressed('V')) {
        // Internal LittleFS disabled — all user files live on SD.
        vol = Volume::SD;
        strncpy(curPath, "/", sizeof(curPath) - 1);
        strncpy(statusMsg, "SD", sizeof(statusMsg) - 1);
        refreshList();
        Display::showToast("SD CARD ONLY", 800);
        SFX::playNav();
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('n') || M5Cardputer.Keyboard.isKeyPressed('N')) {
        // new empty .txt in the current dir, opens straight into EDIT
        char base[24];
        uint16_t stamp = (uint16_t)(millis() % 10000);
        snprintf(base, sizeof(base), "note%u.txt", (unsigned)stamp);
        bufLen = 0;
        buf[0] = '\0';
        cursor = 0;
        dirty = true;
        strncpy(openName, base, sizeof(openName) - 1);
        openName[sizeof(openName) - 1] = '\0';
        phase = Phase::EDIT;
        SFX::play(SFX::CONFIRM);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('x') || M5Cardputer.Keyboard.isKeyPressed('X')) {
        if (entryCount && !entries[sel].isDir) phase = Phase::CONFIRM_DEL;
        return;
    }
    // fR3k v3.0.4: G kicks GPX export of the active GPS track.
    if (M5Cardputer.Keyboard.isKeyPressed('g') || M5Cardputer.Keyboard.isKeyPressed('G')) {
        // Walk /0N3P0rK/gps-*.csv files; the most recent becomes
        // the export source. Result is written to /0N3P0rK/gps.gpx.
        extern int FileMgrMode_gpxExport();
        int n = FileMgrMode_gpxExport();
        SFX::play(n > 0 ? SFX::CONFIRM : SFX::ERROR);
        Display::showToast(n > 0 ? "GPX EXPORTED" : "GPX: NO TRACK", 1100);
        return;
    }
}

void FileMgrMode::handleViewInput() {
    if (!keyNewPress(keyLatch)) return;
    if (keyEsc()) { phase = Phase::BROWSE; refreshList(); return; }
    if (M5Cardputer.Keyboard.isKeyPressed(';')) { if (viewTopLine > 0) viewTopLine--; return; }
    if (M5Cardputer.Keyboard.isKeyPressed('.')) { viewTopLine++; return; }
    if (M5Cardputer.Keyboard.isKeyPressed('e') || M5Cardputer.Keyboard.isKeyPressed('E')) {
        cursor = bufLen;
        phase = Phase::EDIT;
        SFX::playNav();
        return;
    }
}

// line/col of the char at buf[cursor]
uint16_t FileMgrMode::cursorLine() {
    uint16_t line = 0;
    for (uint16_t i = 0; i < cursor && i < bufLen; i++)
        if (buf[i] == '\n') line++;
    return line;
}

void FileMgrMode::handleEditInput() {
    if (!keyNewPress(keyLatch)) return;
    auto& keys = M5Cardputer.Keyboard.keysState();

    if (keyEsc()) {
        if (dirty) {
            if (saveFile()) Display::showToast("SAVED", 700);
            else Display::showToast("SAVE FAIL", 900);
        }
        phase = Phase::BROWSE;
        refreshList();
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
        if (cursor > 0) {
            memmove(buf + cursor - 1, buf + cursor, bufLen - cursor);
            cursor--;
            bufLen--;
            buf[bufLen] = '\0';
            dirty = true;
        }
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(',')) { if (cursor > 0) cursor--; return; }
    if (M5Cardputer.Keyboard.isKeyPressed('/')) { if (cursor < bufLen) cursor++; return; }
    if (M5Cardputer.Keyboard.isKeyPressed(';') || M5Cardputer.Keyboard.isKeyPressed('.')) {
        // up/down = jump to start of prev/next line (simple, no column memory)
        bool up = M5Cardputer.Keyboard.isKeyPressed(';');
        if (up) {
            uint16_t p = cursor;
            while (p > 0 && buf[p - 1] != '\n') p--;
            if (p > 0) { p--; while (p > 0 && buf[p - 1] != '\n') p--; }
            cursor = p;
        } else {
            uint16_t p = cursor;
            while (p < bufLen && buf[p] != '\n') p++;
            if (p < bufLen) p++;
            cursor = p;
        }
        return;
    }
    if (keys.enter) {
        if (bufLen + 1 < EDIT_CAP) {
            memmove(buf + cursor + 1, buf + cursor, bufLen - cursor);
            buf[cursor] = '\n';
            cursor++;
            bufLen++;
            buf[bufLen] = '\0';
            dirty = true;
        }
        return;
    }
    for (char c : keys.word) {
        if (c < 32 || c >= 127) continue;
        if (bufLen + 1 >= EDIT_CAP) break;
        memmove(buf + cursor + 1, buf + cursor, bufLen - cursor);
        buf[cursor] = c;
        cursor++;
        bufLen++;
        buf[bufLen] = '\0';
        dirty = true;
    }
}

void FileMgrMode::update() {
    if (!running) return;
    if (App::windowHidden()) return;

    if (phase == Phase::CONFIRM_DEL) {
        if (!keyNewPress(keyLatch)) return;
        if (M5Cardputer.Keyboard.isKeyPressed('y') || M5Cardputer.Keyboard.isKeyPressed('Y')) {
            char path[PATH_BUF];
            buildFullPath(path, sizeof(path), entries[sel].name);
            fs::FS& fsr = fsFor(vol == Volume::SD);
            fsr.remove(path);
            phase = Phase::BROWSE;
            refreshList();
            Display::showToast("DELETED", 700);
            SFX::play(SFX::CONFIRM);
        } else if (keyEsc() || M5Cardputer.Keyboard.isKeyPressed('n') ||
                   M5Cardputer.Keyboard.isKeyPressed('N')) {
            phase = Phase::BROWSE;
        }
        return;
    }
    if (phase == Phase::BROWSE) handleBrowseInput();
    else if (phase == Phase::VIEW) handleViewInput();
    else if (phase == Phase::EDIT) handleEditInput();
}

void FileMgrMode::getStatusLine(char* buffer, size_t n) {
    if (!buffer || !n) return;
    if (phase == Phase::EDIT)
        snprintf(buffer, n, "EDIT %s%s  ESC SAVE", openName, dirty ? "*" : "");
    else if (phase == Phase::VIEW)
        snprintf(buffer, n, "VIEW %s  E EDIT", openName);
    else
        snprintf(buffer, n, "FILES %s %s  V VOL", vol == Volume::SD ? "SD" : "MEM", curPath);
}

void FileMgrMode::drawBrowse(M5Canvas& canvas) {
    canvas.fillRoundRect(4, 14, 232, 108, 3, UiStyle::BG);
    canvas.drawRoundRect(4, 14, 232, 108, 3, UiStyle::CYAN);
    canvas.setTextColor(UiStyle::CYAN);
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "%s %s", vol == Volume::SD ? "SD" : "MEM", curPath);
    canvas.drawString(hdr, 10, 18);

    if (entryCount == 0) {
        canvas.setTextColor(UiStyle::DIM);
        canvas.drawString("(EMPTY)", 10, 40);
    }
    for (uint8_t i = scroll; i < entryCount && i < scroll + VIS_ROWS; i++) {
        int y = 32 + (i - scroll) * 13;
        bool on = (i == sel);
        if (on) canvas.fillRect(8, y - 1, 224, 13, UiStyle::PINK);
        canvas.setTextColor(on ? UiStyle::BG : (entries[i].isDir ? UiStyle::GOLD : UiStyle::TEXT));
        char row[40];
        if (entries[i].isDir) snprintf(row, sizeof(row), "> %s", entries[i].name);
        else snprintf(row, sizeof(row), "  %s", entries[i].name);
        canvas.drawString(row, 12, y + 1);
        if (!entries[i].isDir) {
            char sz[12];
            snprintf(sz, sizeof(sz), "%luB", (unsigned long)entries[i].size);
            canvas.setTextColor(on ? UiStyle::BG : UiStyle::DIM);
            canvas.drawString(sz, 190, y + 1);
        }
    }
    canvas.setTextColor(UiStyle::DIM);
    canvas.drawString(",/. NAV  ENT OPEN  V VOL  N NEW  X DEL", 8, 112);
}

void FileMgrMode::drawViewEdit(M5Canvas& canvas) {
    canvas.fillRoundRect(4, 14, 232, 108, 3, UiStyle::BG);
    canvas.drawRoundRect(4, 14, 232, 108, 3,
                          phase == Phase::EDIT ? UiStyle::PINK : UiStyle::CYAN);
    canvas.setTextColor(phase == Phase::EDIT ? UiStyle::PINK : UiStyle::CYAN);
    char hdr[44];
    snprintf(hdr, sizeof(hdr), "%s %s%s", phase == Phase::EDIT ? "EDIT" : "VIEW",
             openName, dirty ? "*" : "");
    canvas.drawString(hdr, 10, 18);

    // split into lines, draw a window of them starting at viewTopLine
    canvas.setTextColor(UiStyle::TEXT);
    uint16_t line = 0, i = 0, drawn = 0;
    uint16_t curLine = (phase == Phase::EDIT) ? cursorLine() : viewTopLine;
    uint16_t top = (phase == Phase::EDIT)
                       ? (curLine >= VIS_ROWS ? curLine - VIS_ROWS + 1 : 0)
                       : viewTopLine;
    while (i <= bufLen && drawn < VIS_ROWS) {
        uint16_t start = i;
        while (i < bufLen && buf[i] != '\n') i++;
        if (line >= top) {
            char lbuf[38];
            uint16_t len = (uint16_t)(i - start);
            if (len > sizeof(lbuf) - 1) len = sizeof(lbuf) - 1;
            memcpy(lbuf, buf + start, len);
            lbuf[len] = '\0';
            canvas.drawString(lbuf, 10, 32 + drawn * 13);
            drawn++;
        }
        line++;
        i++;  // skip '\n'
    }
    canvas.setTextColor(UiStyle::DIM);
    if (phase == Phase::VIEW)
        canvas.drawString(";/. SCROLL  E EDIT  ESC BACK", 8, 112);
    else
        canvas.drawString(",;./ MOVE  ENT NEWLINE  ESC SAVE", 8, 112);
}

void FileMgrMode::draw(M5Canvas& canvas) {
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);

    if (phase == Phase::CONFIRM_DEL) {
        canvas.fillRoundRect(30, 40, 180, 50, 3, UiStyle::BG);
        canvas.drawRoundRect(30, 40, 180, 50, 3, UiStyle::RED);
        canvas.setTextColor(UiStyle::RED);
        canvas.drawString("DELETE?", 44, 50);
        canvas.setTextColor(UiStyle::TEXT);
        char nm[30];
        strncpy(nm, entries[sel].name, sizeof(nm) - 1);
        nm[sizeof(nm) - 1] = '\0';
        canvas.drawString(nm, 44, 64);
        canvas.setTextColor(UiStyle::DIM);
        canvas.drawString("Y YES   N/ESC NO", 44, 78);
        return;
    }
    if (phase == Phase::BROWSE) drawBrowse(canvas);
    else drawViewEdit(canvas);
}

// fR3k v3.0.4: GPX export. Walks /0N3P0rK/gps-*.csv (sorted by
// modification time, newest first), picks the most recent track,
// converts every line to a <trkpt>, and writes the result to
// /0N3P0rK/gps.gpx. Returns the number of track points exported,
// or -1 on error. Operator kicks this from FileMgr with the G key.
int FileMgrMode_gpxExport() {
    if (!Storage::available()) return -1;
    File dir = SD.open(Storage::DIR_GPS);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return -1;
    }
    // Find newest gps-*.csv by mtime.
    char newest[64] = {0};
    uint32_t newestMtime = 0;
    File f = dir.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            const char* name = f.name();
            size_t nlen = strlen(name);
            if (nlen > 4 && strcmp(name + nlen - 4, ".csv") == 0 &&
                strncmp(name, "gps-", 4) == 0) {
                uint32_t mt = f.getLastWrite();
                if (mt >= newestMtime) {
                    newestMtime = mt;
                    strncpy(newest, name, sizeof(newest) - 1);
                }
            }
        }
        f.close();
        f = dir.openNextFile();
    }
    dir.close();
    if (!newest[0]) return -1;
    // Open newest + write gpx alongside.
    char srcPath[96];
    snprintf(srcPath, sizeof(srcPath), "%s/%s", Storage::DIR_GPS, newest);
    File src = SD.open(srcPath, FILE_READ);
    if (!src) return -1;
    char outPath[96];
    snprintf(outPath, sizeof(outPath), "%s/gps.gpx", Storage::DIR_GPS);
    File out = SD.open(outPath, FILE_WRITE);
    if (!out) { src.close(); return -1; }
    // GPX 1.1 header.
    out.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    out.println("<gpx version=\"1.1\" creator=\"fR3k v3.0.4\"");
    out.println("     xmlns=\"http://www.topografix.com/GPX/1/1\">");
    out.println("  <trk><name>fR3k</name><trkseg>");
    int n = 0;
    char line[256];
    while (src.available()) {
        size_t L = src.readBytesUntil('\n', line, sizeof(line) - 1);
        line[L] = '\0';
        // CSV columns: timestamp,lat,lon,alt,sats,spd,course,hdop
        // (time-valid path) or uptime-ms,lat,lon,alt,sats,spd,course,hdop
        // (time-invalid path). We only emit <trkpt> when we have a
        // valid lat/lon pair.
        char ts[32] = {0};
        double lat = 0, lon = 0;
        if (line[4] == '-' && line[7] == '-') {
            // ISO 8601 timestamp starts the line.
            char* p = line;
            // copy first 19 chars (YYYY-MM-DDTHH:MM:SS)
            size_t copy = (L < 19) ? L : 19;
            memcpy(ts, p, copy); ts[copy] = '\0';
            // Replace T with space, drop Z. Save as GPX <time>.
            for (size_t i = 0; i < copy; i++) if (ts[i] == 'T') ts[i] = ' ';
            if (copy > 0 && (ts[copy-1] == 'Z' || ts[copy-1] == 'z')) ts[copy-1] = '\0';
            lat = strtod(p + 20, nullptr);
            // find the second comma after lat
            const char* p2 = strchr(p + 20, ',');
            if (p2) lon = strtod(p2 + 1, nullptr);
        } else {
            lat = strtod(line, nullptr);
            const char* p2 = strchr(line, ',');
            if (p2) lon = strtod(p2 + 1, nullptr);
        }
        if (lat == 0.0 && lon == 0.0) continue;
        out.print("    <trkpt lat=\"");
        out.print(lat, 6);
        out.print("\" lon=\"");
        out.print(lon, 6);
        out.print("\">");
        if (ts[0]) {
            out.print("<time>");
            out.print(ts);
            out.print("</time>");
        }
        out.println("</trkpt>");
        n++;
    }
    out.println("  </trkseg></trk>");
    out.println("</gpx>");
    out.close();
    src.close();
    return n;
}
