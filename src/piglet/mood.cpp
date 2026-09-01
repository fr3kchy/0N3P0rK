#include "mood.h"
#include "weather.h"
#include "wolf.h"
#include "../core/config.h"
#include "../core/xp.h"
#include "../core/app.h"
#include "../cap/sniffer.h"
#include "../cap/hc22000.h"
#include "../ui/display.h"
#include "../audio/sfx.h"
#include "../storage/littlefs_ops.h"
#include <Preferences.h>
#include <SD.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int Mood::happiness = 70;
int Mood::hunger = 70;
int Mood::life = 5;  // discrete hearts 0–5
char Mood::currentPhrase[40] = "hello";
uint32_t Mood::lastPhraseChange = 0;
uint32_t Mood::lastActivityTime = 0;
uint32_t Mood::lastDecayMs = 0;
int Mood::lastEffective = 70;

static Preferences s_moodPrefs;
static char s_status[40] = "";
static uint32_t s_statusUntil = 0;
static bool s_moodDirty = false;
static uint32_t s_moodSavedAt = 0;

// fR3k demon voice — short farm, weather and navigation lines for the bubble.
static const char* PH_IDLE[] = {
    "horns online", "tail calibration", "watching the clouds",
    "farm loop stable", "tiny demon, big plans", "trees look suspicious",
    "weather tastes electric", "fR3k never sleeps", "compass wants a walk"
};
static const char* PH_HAPPY[] = {
    "GG, farm secured", "level up", "tail high five", "fruit raid success",
    "200 OK and dancing", "mischief complete", "sunny side achieved"
};
static const char* PH_HUNGRY[] = {
    "404 fruit not found", "low HP, need apples", "snack please",
    "empty fruit table", "need sudo sandwich"
};
static const char* PH_SAD[] = {
    "rain in my horns", "deploy failed again", "segfault in my heart",
    "wolf stole my lunch", "clouds got dramatic"
};
static const char* PH_SLEEPY[] = {
    "admin mode: sleep", "cron job at 3am", "low power horn mode",
    "five more minutes", "dreaming of orchards"
};
static const char* PH_FED[] = {
    "cache is warm now", "HP restored full", "200 OK delicious",
    "fruit buffer filled", "snack ACK received"
};
static const char* PH_PET[] = {
    "horn scratch approved", "more pets more uptime", "good operator yes",
    "tail wags for Freek"
};
static const char* PH_PLAY[] = {
    "catch me if you can", "zoom across the farm", "jump loop engaged",
    "race you to the tree"
};
static const char* PH_BIRD[] = {
    "bird evaded", "feathers everywhere", "sky visitor spotted",
    "bird wins this round"
};

#define COUNT(arr) ((int)(sizeof(arr) / sizeof((arr)[0])))
#define PICK(arr) (arr[random(0, COUNT(arr))])

enum TalkKind : uint8_t {
    TK_IDLE = 0, TK_HAPPY, TK_HUNGRY, TK_SAD, TK_SLEEPY,
    TK_FED, TK_PET, TK_PLAY, TK_BIRD, TK_COUNT
};

static constexpr uint8_t TALK_MAX = 16;
static constexpr uint8_t TALK_LEN = 28;
static char s_talk[TK_COUNT][TALK_MAX][TALK_LEN];
static uint8_t s_talkN[TK_COUNT];

static const char* talkFile(TalkKind k) {
    switch (k) {
        case TK_IDLE:   return "idle.txt";
        case TK_HAPPY:  return "happy.txt";
        case TK_HUNGRY: return "hungry.txt";
        case TK_SAD:    return "sad.txt";
        case TK_SLEEPY: return "sleepy.txt";
        case TK_FED:    return "fed.txt";
        case TK_PET:    return "pet.txt";
        case TK_PLAY:   return "play.txt";
        case TK_BIRD:   return "bird.txt";
        default:        return "idle.txt";
    }
}

static void talkAdd(TalkKind k, const char* line) {
    if (!line || !line[0] || s_talkN[k] >= TALK_MAX) return;
    if (line[0] == '#') return;
    uint8_t n = s_talkN[k];
    strncpy(s_talk[k][n], line, TALK_LEN - 1);
    s_talk[k][n][TALK_LEN - 1] = '\0';
    s_talkN[k] = (uint8_t)(n + 1);
}

static void writeTalkSeed(const char* path, const char* body) {
    if (SD.exists(path)) return;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return;
    f.print(body);
    f.close();
}

static void loadTalkFile(TalkKind k) {
    char path[48];
    snprintf(path, sizeof(path), "%s/%s", Storage::DIR_TALK, talkFile(k));
    File f = SD.open(path, FILE_READ);
    if (!f) return;
    char buf[TALK_LEN];
    uint8_t n = 0;
    while (f.available()) {
        int c = f.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            buf[n] = '\0';
            if (n) talkAdd(k, buf);
            n = 0;
            continue;
        }
        if (n < TALK_LEN - 1) buf[n++] = (char)c;
    }
    if (n) {
        buf[n] = '\0';
        talkAdd(k, buf);
    }
    f.close();
}

static void loadTalkFromSd() {
    memset(s_talkN, 0, sizeof(s_talkN));
    if (!Storage::available()) return;
    Storage::ensureDir(Storage::DIR_TALK);
    writeTalkSeed("/0N3P0rK/talk/idle.txt",
        "# fR3k talk - one line = one bubble\n"
        "# max 24 chars. # starts a comment.\n"
        "horns online\n"
        "watching the clouds\n"
        "compass wants a walk\n");
    writeTalkSeed("/0N3P0rK/talk/happy.txt",
        "fruit raid success\n"
        "GG farm secured\n"
        "mischief complete\n");
    writeTalkSeed("/0N3P0rK/talk/hungry.txt",
        "404 fruit not found\n"
        "need sudo sandwich\n");
    writeTalkSeed("/0N3P0rK/talk/sad.txt",
        "rain in my horns\n"
        "wolf stole my lunch\n");
    writeTalkSeed("/0N3P0rK/talk/sleepy.txt",
        "dreaming of orchards\n"
        "five more minutes\n");
    writeTalkSeed("/0N3P0rK/talk/fed.txt",
        "fruit buffer filled\n"
        "snack ACK received\n");
    writeTalkSeed("/0N3P0rK/talk/pet.txt",
        "horn scratch approved\n"
        "tail wags for Freek\n");
    writeTalkSeed("/0N3P0rK/talk/play.txt",
        "catch me if you can\n"
        "race you to the tree\n");
    writeTalkSeed("/0N3P0rK/talk/bird.txt",
        "feathers everywhere\n"
        "bird wins this round\n");
    for (uint8_t k = 0; k < TK_COUNT; k++) loadTalkFile((TalkKind)k);
    Serial.printf("[TALK] sd lines idle=%u happy=%u\n",
                  (unsigned)s_talkN[TK_IDLE], (unsigned)s_talkN[TK_HAPPY]);
}

static const char* pickMix(const char** built, int bn, TalkKind k) {
    int extra = (int)s_talkN[k];
    int total = bn + extra;
    if (total < 1) return "";
    int i = random(0, total);
    if (i < bn) return built[i];
    return s_talk[k][i - bn];
}

static void clampStat(int& v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
}

void Mood::say(const char* phrase) {
    if (!phrase) return;
    strncpy(currentPhrase, phrase, sizeof(currentPhrase) - 1);
    currentPhrase[sizeof(currentPhrase) - 1] = '\0';
    lastPhraseChange = millis();
}

void Mood::init() {
    lastActivityTime = millis();
    lastDecayMs = millis();
    lastPhraseChange = millis();
    if (s_moodPrefs.begin("pigmood", false)) {
        happiness = s_moodPrefs.getInt("hap", 70);
        hunger = s_moodPrefs.getInt("hun", 70);
        int storedLife = s_moodPrefs.getInt("life", 5);
        // Old builds saved 0–100. New: 0–5 hearts.
        if (storedLife > 5) life = (storedLife + 19) / 20;
        else life = storedLife;
        clampStat(happiness);
        clampStat(hunger);
        if (life < 0) life = 0;
        if (life > 5) life = 5;
    }
    lastEffective = happiness;
    loadTalkFromSd();
    say("hello");
    maybeCureZombie();
    maybeBecomeZombie();
    updateAvatarState();
}

void Mood::saveMood() {
    s_moodPrefs.putInt("hap", happiness);
    s_moodPrefs.putInt("hun", hunger);
    s_moodPrefs.putInt("life", life);
    s_moodDirty = false;
    s_moodSavedAt = millis();
}

void Mood::adjustHappiness(int delta) {
    happiness += delta;
    clampStat(happiness);
    lastEffective = happiness;
}

int Mood::getCurrentHappiness() { return happiness; }
int Mood::getEffectiveHappiness() { return happiness; }
int Mood::getLastEffectiveHappiness() { return lastEffective; }
int Mood::getHunger() { return hunger; }
int Mood::getHearts() { return life; }
int Mood::getLife() { return life * 20; }
uint32_t Mood::getLastActivityTime() { return lastActivityTime; }
const char* Mood::getCurrentPhrase() {
    if (s_status[0] && millis() < s_statusUntil) return s_status;
    return currentPhrase;
}

void Mood::setStatusMessage(const char* msg) {
    if (!msg) {
        s_status[0] = '\0';
        return;
    }
    strncpy(s_status, msg, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
    s_statusUntil = millis() + 4000;
}

int Mood::addFood(int amount) {
    int gained = 0;
    if (amount < 1) return 0;
    hunger += amount;
    while (hunger >= 100 && life < 5) {
        hunger -= 100;
        life += 1;
        gained++;
    }
    // Keep a food bar after a new heart so hunger=0 does not eat it next tick.
    if (gained && hunger < 40) hunger = 40;
    if (life >= 5 && hunger > 100) hunger = 100;
    if (hunger < 0) hunger = 0;
    return gained;
}

void Mood::maybeCureZombie() {
    if (life < 5) return;
    if (Config::personality().pigSkin != (uint8_t)PigSkin::ZOMBIE) return;
    Config::cureZombie();
    Display::showToast("PIG AGAIN!", 2000);
    Display::notify(NoticeKind::REWARD, "P1G AGAIN", 3000, NoticeChannel::TOP_BAR);
}

void Mood::maybeBecomeZombie() {
    if (life > 0) return;
    if (Config::personality().pigSkin == (uint8_t)PigSkin::ZOMBIE) return;
    Config::becomeZombie();
    hunger = 45;
}

void Mood::feed() {
    int gained = addFood(30);
    happiness += 6;
    clampStat(happiness);
    lastActivityTime = millis();
    lastEffective = happiness;
    say(pickMix(PH_FED, COUNT(PH_FED), TK_FED));
    SFX::playPersonality();
    Avatar::sniff();
    XP::addXP(XPEvent::FEED);
    if (gained) {
        Display::showToast(gained == 1 ? "+1 HEART" : "+HEARTS", 900);
        maybeCureZombie();
    }
    saveMood();
    updateAvatarState();
}

void Mood::eatWorld() {
    int gained = addFood(5);
    happiness += 2;
    clampStat(happiness);
    lastActivityTime = millis();
    lastEffective = happiness;
    // A pile of fruit can land 4 picks in one frame — one oink/sniff, not four.
    static uint32_t lastFx = 0;
    uint32_t now = millis();
    if ((uint32_t)(now - lastFx) >= 280) {
        lastFx = now;
        say(pickMix(PH_FED, COUNT(PH_FED), TK_FED));
        SFX::playPersonality();
        Avatar::sniff();
    }
    if (gained) {
        Display::showToast(gained == 1 ? "+1 HEART" : "+HEARTS", 900);
        maybeCureZombie();
        saveMood();
    } else {
        s_moodDirty = true;
    }
    updateAvatarState();
}

void Mood::hurt(int amount) {
    if (amount < 1) amount = 1;
    // amount is hearts. Old call sites that passed 18 still mean 1 heart.
    if (amount > 5) amount = 1;
    life -= amount;
    if (life < 0) life = 0;
    happiness -= 10 * amount;
    clampStat(happiness);
    lastEffective = happiness;
    say("segfault");
    Avatar::flinch();
    Avatar::setState(AvatarState::SAD);
    maybeBecomeZombie();
    saveMood();
}

void Mood::pet() {
    happiness += 14;
    clampStat(happiness);
    lastActivityTime = millis();
    lastEffective = happiness;
    say(pickMix(PH_PET, COUNT(PH_PET), TK_PET));
    SFX::playPersonality();
    Avatar::wiggleEars();
    Avatar::triggerTailWiggle();
    XP::addXP(XPEvent::PET);
    saveMood();
    updateAvatarState();
}

void Mood::play() {
    happiness += 4;
    hunger -= 2;
    clampStat(happiness);
    clampStat(hunger);
    lastActivityTime = millis();
    lastEffective = happiness;
    if ((millis() - lastPhraseChange) > 2500)
        say(pickMix(PH_PLAY, COUNT(PH_PLAY), TK_PLAY));
    updateAvatarState();
}

void Mood::onBirdKill() {
    happiness += 8;
    clampStat(happiness);
    lastActivityTime = millis();
    lastEffective = happiness;
    say(pickMix(PH_BIRD, COUNT(PH_BIRD), TK_BIRD));
    Avatar::cuteJump();
    Avatar::triggerSparkles(5);
}

void Mood::onIdle() {
    updateAvatarState();
}

void Mood::updateAvatarState() {
    Avatar::setMoodIntensity(happiness - 50);
    if (Config::personality().animTest && App::mode() == AppMode::FARM) return;

    // AGGRO / pinned hunt: no new HS ~45s → ANGRY (was test-only).
    // New pair → celebrate. Mic-dance stays unused.
    static uint32_t s_huntStamp = 0;
    static uint16_t s_huntPairs = 0;
    const bool hunting =
        Cap::isRunning() &&
        (Cap::runMode() == Cap::RunMode::Aggressive ||
         Cap::runMode() == Cap::RunMode::Pinned);
    if (hunting) {
        uint16_t pairs = Hc22000::pairCount();
        if (s_huntStamp == 0) {
            s_huntStamp = millis();
            s_huntPairs = pairs;
        } else if (pairs > s_huntPairs) {
            s_huntPairs = pairs;
            s_huntStamp = millis();
            Avatar::cuteJump();
            Avatar::triggerSparkles(5);
            Avatar::triggerTailWiggle();
            Avatar::setState(AvatarState::EXCITED);
            return;
        } else if ((uint32_t)(millis() - s_huntStamp) >= 45000u) {
            Avatar::setState(AvatarState::ANGRY);
            return;
        } else {
            Avatar::setState(AvatarState::HUNTING);
            return;
        }
    } else {
        s_huntStamp = 0;
    }

    if (hunger < 22 || happiness < 18) {
        Avatar::setState(AvatarState::SAD);
    } else if (Avatar::isNightTime() && (millis() - lastActivityTime) > 20000) {
        Avatar::setState(AvatarState::SLEEPY);
    } else if (happiness > 82 && hunger > 50) {
        Avatar::setState(AvatarState::EXCITED);
    } else if (happiness > 60) {
        Avatar::setState(AvatarState::HAPPY);
    } else {
        Avatar::setState(AvatarState::NEUTRAL);
    }
}

void Mood::pickPhrase() {
    if (hunger < 25) {
        say(pickMix(PH_HUNGRY, COUNT(PH_HUNGRY), TK_HUNGRY));
    } else if (happiness < 25) {
        say(pickMix(PH_SAD, COUNT(PH_SAD), TK_SAD));
    } else if (Avatar::isNightTime() && (millis() - lastActivityTime) > 25000) {
        say(pickMix(PH_SLEEPY, COUNT(PH_SLEEPY), TK_SLEEPY));
    } else if (happiness > 75) {
        say(pickMix(PH_HAPPY, COUNT(PH_HAPPY), TK_HAPPY));
    } else {
        say(pickMix(PH_IDLE, COUNT(PH_IDLE), TK_IDLE));
    }
}

void Mood::update() {
    uint32_t now = millis();

    static bool s_wasNight = false;
    const bool night = Avatar::isNightTime();
    if (s_wasNight && !night && life > 0)
        XP::addXP(XPEvent::NIGHT_SURVIVE);
    s_wasNight = night;

    if (now - lastDecayMs >= 10000) {
        lastDecayMs = now;
        hunger -= 4;
        clampStat(hunger);
        if (hunger == 0 && life > 0) {
            life -= 1;
            say("tummy empty");
            Avatar::flinch();
            maybeBecomeZombie();
        }
        if ((now - lastActivityTime) > 90000) happiness -= 2;
        clampStat(happiness);
        lastEffective = happiness;
        saveMood();
    }

    if (s_moodDirty && (uint32_t)(now - s_moodSavedAt) >= 2000)
        saveMood();

    // PIG → TALK SEC (2..10): gap until next automatic monologue.
    uint32_t talkMs = (uint32_t)Config::personality().talkIntervalSec * 1000u;
    if (talkMs < 2000) talkMs = 2000;
    if (talkMs > 10000) talkMs = 10000;
    if (now - lastPhraseChange >= talkMs) {
        pickPhrase();
    }

    if ((now - lastActivityTime) > 40000 && hunger > 40 && random(0, 200) == 0) {
        Avatar::pawScratch();
    }

    if (Config::personality().freeLife &&
        !Avatar::isControlLocked() &&
        !Avatar::isPlayDead()) {
        if (Wolf::isActive()) {
            Avatar::fleeToHide();
        } else if (hunger < 32 && !Avatar::isSitting() && !Avatar::isHiding()) {
            Avatar::walkToFood();
        }
    }

    updateAvatarState();
}

void Mood::draw(M5Canvas& canvas) {
    if (Avatar::isTransitioning()) return;
    {
        uint32_t talkMs = (uint32_t)Config::personality().talkIntervalSec * 1000u;
        if (talkMs < 2000) talkMs = 2000;
        if (talkMs > 10000) talkMs = 10000;
        uint32_t showMs = talkMs > 400 ? talkMs - 400 : talkMs;
        if (showMs < 1200) showMs = 1200;
        if ((millis() - lastPhraseChange) >= showMs) return;
    }

    const char* ph = getCurrentPhrase();
    if (!ph || !ph[0]) return;

    int chars = (int)strlen(ph);
    int bubbleW = chars * 6 + 12;
    if (bubbleW < 44) bubbleW = 44;
    if (bubbleW > 168) bubbleW = 168;
    int bubbleH = 16;
    int pigX = Avatar::getCurrentX();
    int bubbleX = pigX + 20;
    int bubbleY = 8;
    if (bubbleX + bubbleW > 236) bubbleX = pigX - bubbleW - 4;
    if (bubbleX < 2) bubbleX = 2;

    uint16_t fg = 0xEF5D;
    uint16_t bg = 0x2145;
    if (Weather::getActiveSeason() == Season::RETRO) {
        fg = 0xE73C;
        bg = 0x1082;
    } else if (Weather::getActiveSeason() == Season::NOIR) {
        fg = 0xFE60;
        bg = 0x1082;
    }

    canvas.fillRoundRect(bubbleX, bubbleY, bubbleW, bubbleH, 4, fg);
    canvas.fillTriangle(bubbleX + 12, bubbleY + bubbleH,
                        bubbleX + 20, bubbleY + bubbleH,
                        bubbleX + 16, bubbleY + bubbleH + 5, fg);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(bg);
    canvas.setTextSize(1);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString(ph, bubbleX + 6, bubbleY + 4);
}
