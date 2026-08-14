#include "mood.h"
#include "weather.h"
#include "wolf.h"
#include "../core/config.h"
#include "../audio/sfx.h"
#include <Preferences.h>
#include <string.h>
#include <stdlib.h>

int Mood::happiness = 70;
int Mood::hunger = 70;
int Mood::life = 100;
char Mood::currentPhrase[40] = "oink";
uint32_t Mood::lastPhraseChange = 0;
uint32_t Mood::lastActivityTime = 0;
uint32_t Mood::lastDecayMs = 0;
int Mood::lastEffective = 70;

static Preferences s_moodPrefs;
static char s_status[40] = "";
static uint32_t s_statusUntil = 0;

static const char* PH_IDLE[] = {
    "oink", "snuffle", "zzz...", "hm?", "warm dirt",
    "nose twitch", "soft grunt", "apple?", "nap soon"
};
static const char* PH_HAPPY[] = {
    "wee!", "best day", "tail go", "love this", "hehe"
};
static const char* PH_HUNGRY[] = {
    "feed me", "tummy...", "truffle?", "so empty", "please"
};
static const char* PH_SAD[] = {
    "lonely", "bored hog", "hey...", "miss you", "sit with me"
};
static const char* PH_SLEEPY[] = {
    "yawn", "eyes heavy", "blanket", "five more min", "zzz"
};
static const char* PH_FED[] = {
    "nom nom", "full now", "thank u", "crunch!", "happy tum"
};
static const char* PH_PET[] = {
    "hehehe", "ear wiggle", "more pets", "best human", "purr-oink"
};
static const char* PH_PLAY[] = {
    "zoom!", "again!", "catch me", "wee jump", "grass run"
};
static const char* PH_BIRD[] = {
    "gotcha!", "feather!", "nice shot", "oink boom"
};

#define PICK(arr) (arr[random(0, (int)(sizeof(arr) / sizeof(arr[0])))])

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
        life = s_moodPrefs.getInt("life", 100);
        clampStat(happiness);
        clampStat(hunger);
        clampStat(life);
    }
    lastEffective = happiness;
    say("oink oink");
    updateAvatarState();
}

void Mood::saveMood() {
    s_moodPrefs.putInt("hap", happiness);
    s_moodPrefs.putInt("hun", hunger);
    s_moodPrefs.putInt("life", life);
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
int Mood::getLife() { return life; }
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

void Mood::feed() {
    hunger += 28;
    happiness += 6;
    life += 4;
    clampStat(hunger);
    clampStat(happiness);
    clampStat(life);
    lastActivityTime = millis();
    lastEffective = happiness;
    say(PICK(PH_FED));
    SFX::play(SFX::OINK_HAPPY);
    Avatar::sniff();
    saveMood();
    updateAvatarState();
}

void Mood::eatWorld() {
    hunger += 10;
    life += 2;
    happiness += 2;
    clampStat(hunger);
    clampStat(life);
    clampStat(happiness);
    lastActivityTime = millis();
    lastEffective = happiness;
    if ((millis() - lastPhraseChange) > 2000) say(PICK(PH_FED));
    saveMood();
    updateAvatarState();
}

void Mood::hurt(int amount) {
    if (amount < 0) amount = 0;
    life -= amount;
    happiness -= amount / 2;
    clampStat(life);
    clampStat(happiness);
    lastEffective = happiness;
    if (life < 25) say("ow...");
    saveMood();
    updateAvatarState();
}

void Mood::pet() {
    happiness += 14;
    clampStat(happiness);
    lastActivityTime = millis();
    lastEffective = happiness;
    say(PICK(PH_PET));
    SFX::play(SFX::OINK_CURIOUS);
    Avatar::wiggleEars();
    Avatar::triggerTailWiggle();
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
    if ((millis() - lastPhraseChange) > 2500) say(PICK(PH_PLAY));
    updateAvatarState();
}

void Mood::onBirdKill() {
    happiness += 8;
    clampStat(happiness);
    lastActivityTime = millis();
    lastEffective = happiness;
    say(PICK(PH_BIRD));
    Avatar::cuteJump();
    Avatar::triggerSparkles(5);
}

void Mood::onIdle() {
    updateAvatarState();
}

void Mood::updateAvatarState() {
    Avatar::setMoodIntensity(happiness - 50);
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
        say(PICK(PH_HUNGRY));
    } else if (happiness < 25) {
        say(PICK(PH_SAD));
    } else if (Avatar::isNightTime() && (millis() - lastActivityTime) > 25000) {
        say(PICK(PH_SLEEPY));
    } else if (happiness > 75) {
        say(PICK(PH_HAPPY));
    } else {
        say(PICK(PH_IDLE));
    }
}

void Mood::update() {
    uint32_t now = millis();

    if (now - lastDecayMs >= 45000) {
        lastDecayMs = now;
        hunger -= 3;
        if (hunger < 12) life -= 2;
        else if (hunger > 70 && life < 100) life += 1;
        if ((now - lastActivityTime) > 90000) happiness -= 2;
        clampStat(hunger);
        clampStat(happiness);
        clampStat(life);
        lastEffective = happiness;
        saveMood();
    }

    if (now - lastPhraseChange >= 18000) {
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
    if ((millis() - lastPhraseChange) >= 5000) return;

    const char* ph = getCurrentPhrase();
    if (!ph || !ph[0]) return;

    int chars = (int)strlen(ph);
    int bubbleW = chars * 6 + 12;
    if (bubbleW < 44) bubbleW = 44;
    if (bubbleW > 120) bubbleW = 120;
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
    }

    canvas.fillRoundRect(bubbleX, bubbleY, bubbleW, bubbleH, 4, fg);
    canvas.fillTriangle(bubbleX + 12, bubbleY + bubbleH,
                        bubbleX + 20, bubbleY + bubbleH,
                        bubbleX + 16, bubbleY + bubbleH + 5, fg);
    canvas.setTextColor(bg);
    canvas.setTextSize(1);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString(ph, bubbleX + 6, bubbleY + 4);
}
