#include "mood.h"
#include "weather.h"
#include "wolf.h"
#include "../core/config.h"
#include "../ui/display.h"
#include "../audio/sfx.h"
#include <Preferences.h>
#include <string.h>
#include <stdlib.h>

int Mood::happiness = 70;
int Mood::hunger = 70;
int Mood::life = 5;  // discrete hearts 0–5
char Mood::currentPhrase[32] = "привет";
uint32_t Mood::lastPhraseChange = 0;
uint32_t Mood::lastActivityTime = 0;
uint32_t Mood::lastDecayMs = 0;
int Mood::lastEffective = 70;

static Preferences s_moodPrefs;
static char s_status[40] = "";
static uint32_t s_statusUntil = 0;

// Readable first. One rare leet line per pile so 0n3 style stays a wink.
static const char* PH_IDLE[] = {
    "привет",
    "hello friend",
    "я не трогал",
    "это фича",
    "dns виноват",
    "логи молчат",
    "prod вроде жив",
    "оно компилится",
    "sudo oink",
    "wifi go brrr",
    "nothing is real",
    "0n3 прив3т"
};
static const char* PH_HAPPY[] = {
    "я в системе",
    "access granted",
    "it works!!",
    "бог админ",
    "легенда",
    "gg wp",
    "hack the planet",
    "this guy oinks"
};
static const char* PH_HUNGRY[] = {
    "404 яблоко",
    "диск полный :(",
    "нужен sudo еда",
    "нет пакетов",
    "тумми empty",
    "low hp tum"
};
static const char* PH_SAD[] = {
    "deploy failed",
    "prod лежит",
    "это не я",
    "blame dns",
    "ticket #404",
    "conn refused"
};
static const char* PH_SLEEPY[] = {
    "админ спит",
    "cron at 3am",
    "zzz ещё 5 мин",
    "standby...",
    "screen saver"
};
static const char* PH_FED[] = {
    "nom nom",
    "200 ok yum",
    "сыр это жизнь",
    "cache warm",
    "crunch!"
};
static const char* PH_PET[] = {
    "hehehe",
    "more pets",
    "best haxor",
    "purr-oink"
};
static const char* PH_PLAY[] = {
    "zoom!",
    "catch me",
    "hack the planet",
    "again!",
    "ping flood"
};
static const char* PH_BIRD[] = {
    "gotcha!",
    "nice shot",
    "pkt dropped",
    "oink boom"
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
    say("привет");
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
    if (life >= 5 && hunger > 100) hunger = 100;
    if (hunger < 0) hunger = 0;
    return gained;
}

void Mood::feed() {
    int gained = addFood(30);
    happiness += 6;
    clampStat(happiness);
    lastActivityTime = millis();
    lastEffective = happiness;
    say(PICK(PH_FED));
    SFX::play(SFX::OINK_HAPPY);
    Avatar::sniff();
    if (gained) Display::showToast(gained == 1 ? "+1 HEART" : "+HEARTS", 900);
    saveMood();
    updateAvatarState();
}

void Mood::eatWorld() {
    int gained = addFood(20);
    happiness += 2;
    clampStat(happiness);
    lastActivityTime = millis();
    lastEffective = happiness;
    say(PICK(PH_FED));
    SFX::play(SFX::OINK_HAPPY);
    Avatar::sniff();
    if (gained) Display::showToast(gained == 1 ? "+1 HEART" : "+HEARTS", 900);
    saveMood();
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

    if (now - lastDecayMs >= 10000) {
        lastDecayMs = now;
        hunger -= 4;
        clampStat(hunger);
        if (hunger == 0 && life > 0) {
            life -= 1;
            say("тумми empty");
        }
        if ((now - lastActivityTime) > 90000) happiness -= 2;
        clampStat(happiness);
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
