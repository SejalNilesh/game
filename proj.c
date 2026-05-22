#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <windows.h>
#include <mmsystem.h>

#include <GL/glut.h>
#include <GL/glu.h>
#include <GL/gl.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* ──────────────────────────────────────────── constants ── */
#define INIT_W 1100
#define INIT_H 800

#define MAX_GRID  6
#define MAX_CARDS (MAX_GRID * MAX_GRID)
#define SCORE_FILE "score.txt"

/* ──────────────────────────────────────────── sound ───────
 * We generate tiny in-memory WAV files and pipe them to aplay (Linux)
 * or afplay (macOS). No external sound files needed.
 * ─────────────────────────────────────────────────────────── */
#include <stdint.h>
#define MAX_PLAYERS 50

typedef struct {
    char name[50];
    int time;
    int moves;
    float accuracy;
} Player;

Player leaderboard[MAX_PLAYERS];
int playerCount = 0;

char currentPlayer[50] = "";

/* ── Leaderboard animation state ── */
static float lbRevealTime   = 0.0f;   /* counts up from 0 when entering leaderboard */
static float lbFireworkTimer = 0.0f;  /* spawns firework particles periodically */
static int   lbHighlightRank = -1;    /* which rank the current player is at (-1=none) */

/* ── Sound helpers using MCI (supports MP3 files) ─────────
 * The .wav files are actually MP3s, so PlaySound can't handle them.
 * mciSendString from winmm supports MP3 playback natively.
 * ─────────────────────────────────────────────────────────── */
#pragma comment(lib, "winmm.lib")

/* Build absolute path next to the .exe for a sound file */
static void getSoundPath(const char *filename, char *outPath, int maxLen) {
    char exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    /* strip executable name to get directory */
    char *lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    snprintf(outPath, maxLen, "%s%s", exePath, filename);
}

void playCorrectSound() {
    char path[MAX_PATH];
    char cmd[MAX_PATH + 64];

    getSoundPath("correct.wav", path, MAX_PATH);

    /* close any previous instance of this alias */
    mciSendString("close correctSnd", NULL, 0, NULL);

    snprintf(cmd, sizeof(cmd), "open \"%s\" type mpegvideo alias correctSnd", path);
    MCIERROR err = mciSendString(cmd, NULL, 0, NULL);
    if (err) {
        printf("MCI open error for correct.wav: %ld\n", (long)err);
        return;
    }
    mciSendString("play correctSnd from 0", NULL, 0, NULL);
    printf("Playing correct.wav via MCI\n");
}

void playWrongSound() {
    char path[MAX_PATH];
    char cmd[MAX_PATH + 64];

    getSoundPath("wrong.wav", path, MAX_PATH);

    mciSendString("close wrongSnd", NULL, 0, NULL);

    snprintf(cmd, sizeof(cmd), "open \"%s\" type mpegvideo alias wrongSnd", path);
    MCIERROR err = mciSendString(cmd, NULL, 0, NULL);
    if (err) {
        printf("MCI open error for wrong.wav: %ld\n", (long)err);
        return;
    }
    mciSendString("play wrongSnd from 0", NULL, 0, NULL);
    printf("Playing wrong.wav via MCI\n");
}

/* 18 food items; matching symbolLabel[] order */
static const char *textureFiles[] = {
    "textures/pizza.jpg",
    "textures/burger.jpg",
    "textures/bento.jpg",
    "textures/pasta.jpg",
    "textures/cake.jpg",
    "textures/donut.jpg",
    "textures/icecream.jpg",
    "textures/chicken.jpg",
    "textures/taco.jpg",
    "textures/stew.jpg",
    "textures/waffle.jpg",
    "textures/bagel.jpg",
    "textures/wok.jpg",
    "textures/pie.jpg",
    "textures/wine.jpg",
    "textures/popcorn.jpg",
    "textures/pretzel.jpg",
    "textures/falafel.jpg",
};
#define NUM_TEXTURES 18
static GLuint texIDs[NUM_TEXTURES];
static int    texLoaded[NUM_TEXTURES];

/* fallback labels when texture is missing */
static const char *symbolLabel[] = {
    "Pizza","Burger","Bento","Pasta","Cake",
    "Donut","IceCrm","Chicken","Taco","Stew",
    "Waffle","Bagel","Wok","Pie","Wine",
    "Popcorn","Pretzel","Falafel"
};

static void loadTextures(void) {
    glGenTextures(NUM_TEXTURES, texIDs);
    for (int i = 0; i < NUM_TEXTURES; i++) {
        int w, h, ch;
        unsigned char *data = stbi_load(textureFiles[i], &w, &h, &ch, 4);
        if (!data) {
            printf("[tex] missing: %s  (will use text fallback)\n", textureFiles[i]);
            texLoaded[i] = 0;
            continue;
        }
        glBindTexture(GL_TEXTURE_2D, texIDs[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
        texLoaded[i] = 1;
    }
}

/* draw textured quad (y axis: y is TOP, y-h is BOTTOM in our coord system) */
static void drawTexturedQuad(float x, float y, float w, float h, int texIdx, float alpha) {
    if (texIdx < 0 || texIdx >= NUM_TEXTURES || !texLoaded[texIdx]) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texIDs[texIdx]);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2f(x,   y);
    glTexCoord2f(1,0); glVertex2f(x+w, y);
    glTexCoord2f(1,1); glVertex2f(x+w, y-h);
    glTexCoord2f(0,1); glVertex2f(x,   y-h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

/* ──────────────────────────────────────────── particles ── */
#define MAX_PARTICLES 300
typedef struct {
    float x, y, vx, vy;
    float r, g, b, a;
    float life, size;
} Particle;
static Particle particles[MAX_PARTICLES];
static int particleCount = 0;

static void spawnParticles(float cx, float cy, float r, float g, float b) {
    for (int k = 0; k < 22; k++) {
        if (particleCount >= MAX_PARTICLES) break;
        Particle *p = &particles[particleCount++];
        float angle = ((float)(rand() % 360)) * 3.14159f / 180.0f;
        float speed = 2.0f + (rand() % 100) * 0.05f;
        p->x=cx; p->y=cy;
        p->vx=cosf(angle)*speed; p->vy=sinf(angle)*speed;
        p->r=r; p->g=g; p->b=b; p->a=1.0f;
        p->life=1.0f;
        p->size=5.0f+(rand()%7);
    }
}

/* Firework-style burst with more particles and bigger spread */
static void spawnFirework(float cx, float cy) {
    float colors[][3] = {
        {1.0f, 0.85f, 0.1f},  /* gold */
        {1.0f, 0.4f, 0.1f},   /* orange */
        {0.3f, 1.0f, 0.5f},   /* green */
        {0.4f, 0.6f, 1.0f},   /* blue */
        {1.0f, 0.3f, 0.6f},   /* pink */
    };
    int ci = rand() % 5;
    for (int k = 0; k < 35; k++) {
        if (particleCount >= MAX_PARTICLES) break;
        Particle *p = &particles[particleCount++];
        float angle = ((float)(rand() % 360)) * 3.14159f / 180.0f;
        float speed = 3.0f + (rand() % 150) * 0.04f;
        p->x = cx; p->y = cy;
        p->vx = cosf(angle) * speed;
        p->vy = sinf(angle) * speed;
        p->r = colors[ci][0] + (rand() % 20) * 0.01f;
        p->g = colors[ci][1] + (rand() % 20) * 0.01f;
        p->b = colors[ci][2] + (rand() % 20) * 0.01f;
        p->a = 1.0f;
        p->life = 0.8f + (rand() % 40) * 0.01f;
        p->size = 4.0f + (rand() % 10);
    }
}

static void updateParticles(void) {
    for (int i=particleCount-1; i>=0; i--) {
        Particle *p=&particles[i];
        p->x+=p->vx; p->y+=p->vy;
        p->vy+=0.14f;
        p->life-=0.022f;
        p->a=p->life;
        if (p->life<=0.0f) particles[i]=particles[--particleCount];
    }
}

static void drawParticles(void) {
    glBegin(GL_QUADS);
    for (int i=0; i<particleCount; i++) {
        Particle *p=&particles[i];
        float s=p->size*p->life;
        glColor4f(p->r,p->g,p->b,p->a);
        glVertex2f(p->x-s,p->y-s); glVertex2f(p->x+s,p->y-s);
        glVertex2f(p->x+s,p->y+s); glVertex2f(p->x-s,p->y+s);
    }
    glEnd();
}

/* ──────────────────────────────────────────── stars ─────── */
#define NUM_STARS 130
typedef struct { float x,y,brightness,speed,phase; } Star;
static Star stars[NUM_STARS];
static float globalTime=0.0f;

static void initStars(void) {
    for(int i=0;i<NUM_STARS;i++){
        stars[i].x=(float)(rand()%1100);
        stars[i].y=(float)(rand()%800);
        stars[i].brightness=0.3f+(rand()%70)*0.01f;
        stars[i].speed=0.01f+(rand()%30)*0.001f;
        stars[i].phase=(float)(rand()%628)*0.01f;
    }
}

static void drawStars(void){
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for(int i=0;i<NUM_STARS;i++){
        float b=stars[i].brightness*(0.6f+0.4f*sinf(globalTime*stars[i].speed*10.0f+stars[i].phase));
        glColor4f(b,b,b+0.1f,b);
        glVertex2f(stars[i].x,stars[i].y);
    }
    glEnd();
}

/* ──────────────────────────────────────────── celebration ─ */
/* floating "PERFECT ✓" banners that appear on match */
#define MAX_BANNERS 8
typedef struct {
    float x, y, life;    /* life 0..1, counts down */
    float vy;
    int   active;
} Banner;
static Banner banners[MAX_BANNERS];

static void spawnBanner(float cx, float cy) {
    for(int i=0;i<MAX_BANNERS;i++){
        if(!banners[i].active){
            banners[i].x=cx; banners[i].y=cy;
            banners[i].life=1.0f;
            banners[i].vy=-1.8f;
            banners[i].active=1;
            return;
        }
    }
}

static void updateBanners(void) {
    for(int i=0;i<MAX_BANNERS;i++){
        if(!banners[i].active) continue;
        banners[i].y+=banners[i].vy;
        banners[i].life-=0.012f;
        if(banners[i].life<=0.0f) banners[i].active=0;
    }
}

static void drawBanners(void) {
    void *font=GLUT_BITMAP_TIMES_ROMAN_24;
    const char *msg="PERFECT!";
    for(int i=0;i<MAX_BANNERS;i++){
        if(!banners[i].active) continue;
        float alpha=banners[i].life;
        float scale=1.0f+0.3f*(1.0f-banners[i].life);
        /* glow */
        glColor4f(0.4f,1.0f,0.5f,alpha*0.35f);
        int tw=0; for(const char *c=msg;*c;c++) tw+=glutBitmapWidth(font,*c);
        float bx=banners[i].x-(float)tw*0.5f*scale;
        float by=banners[i].y;
        /* shadow */
        glColor4f(0.0f,0.0f,0.0f,alpha*0.4f);
        glRasterPos2f(bx+2,by+2);
        for(const char *c=msg;*c;c++) glutBitmapCharacter(font,*c);
        /* main text */
        glColor4f(0.3f,1.0f,0.55f,alpha);
        glRasterPos2f(bx,by);
        for(const char *c=msg;*c;c++) glutBitmapCharacter(font,*c);
    }
}

/* ──────────────────────────────────────────── screen flash ─
 * A full-screen colour overlay that fades out quickly.
 * Used for CORRECT (green) and INCORRECT (red) feedback.
 * ─────────────────────────────────────────────────────────── */
/* forward decls needed by drawFlash */
static int winW, winH;   /* defined again in game state – extern resolves fine in C */
typedef struct {
    float r, g, b;
    float alpha;      /* starts high, decays to 0 */
    float decay;      /* alpha subtracted per frame */
} ScreenFlash;
static ScreenFlash screenFlash = {0};

static void triggerFlash(float r, float g, float b, float alpha, float decay) {
    screenFlash.r = r;
    screenFlash.g = g;
    screenFlash.b = b;
    screenFlash.alpha = alpha;
    screenFlash.decay = decay;
}

static void updateFlash(void) {
    if (screenFlash.alpha > 0.0f) {
        screenFlash.alpha -= screenFlash.decay;
        if (screenFlash.alpha < 0.0f) screenFlash.alpha = 0.0f;
    }
}

static void drawFlash(void) {
    if (screenFlash.alpha < 0.005f) return;
    glColor4f(screenFlash.r, screenFlash.g, screenFlash.b, screenFlash.alpha);
    glBegin(GL_QUADS);
    glVertex2f(0,0); glVertex2f((float)winW,0);
    glVertex2f((float)winW,(float)winH); glVertex2f(0,(float)winH);
    glEnd();
}

/* ──────────────────────────────────────────── card shake ───
 * Wrong cards oscillate horizontally with damped sine.
 * shakeTime counts DOWN from 1.0 to 0 over ~0.75 s
 * ─────────────────────────────────────────────────────────── */
static float cardShakeTime[MAX_CARDS];   /* per-card countdown 1→0 */
#define SHAKE_FREQ   28.0f               /* oscillations per second */
#define SHAKE_AMP    18.0f               /* peak pixel displacement */

static void startCardShake(int idx) {
    if (idx >= 0 && idx < MAX_CARDS) cardShakeTime[idx] = 1.0f;
}

static float getCardShakeOffset(int idx) {
    if (idx < 0 || idx >= MAX_CARDS) return 0.0f;
    float t = cardShakeTime[idx];
    if (t <= 0.0f) return 0.0f;
    return SHAKE_AMP * t * sinf(SHAKE_FREQ * (1.0f - t) * 3.14159f * 2.0f);
}

/* ── Wrong-match "NOPE!" banner (separate pool, red coloured) ── */
#define MAX_ERR_BANNERS 4
typedef struct {
    float x, y, life;
    float vy;
    int   active;
} ErrBanner;
static ErrBanner errBanners[MAX_ERR_BANNERS];

static void spawnErrBanner(float cx, float cy) {
    for (int i = 0; i < MAX_ERR_BANNERS; i++) {
        if (!errBanners[i].active) {
            errBanners[i].x = cx; errBanners[i].y = cy;
            errBanners[i].life = 1.0f;
            errBanners[i].vy  = -2.2f;
            errBanners[i].active = 1;
            return;
        }
    }
}

static void updateErrBanners(void) {
    for (int i = 0; i < MAX_ERR_BANNERS; i++) {
        if (!errBanners[i].active) continue;
        errBanners[i].y    += errBanners[i].vy;
        errBanners[i].life -= 0.014f;
        if (errBanners[i].life <= 0.0f) errBanners[i].active = 0;
    }
}

static void drawErrBanners(void) {
    void *font = GLUT_BITMAP_TIMES_ROMAN_24;
    const char *msg = "NOPE!";
    for (int i = 0; i < MAX_ERR_BANNERS; i++) {
        if (!errBanners[i].active) continue;
        float alpha = errBanners[i].life;
        int tw = 0;
        for (const char *c = msg; *c; c++) tw += glutBitmapWidth(font, *c);
        float bx = errBanners[i].x - (float)tw * 0.5f;
        float by = errBanners[i].y;
        /* shadow */
        glColor4f(0.0f, 0.0f, 0.0f, alpha * 0.5f);
        glRasterPos2f(bx + 2, by + 2);
        for (const char *c = msg; *c; c++) glutBitmapCharacter(font, *c);
        /* main – vivid red-orange */
        glColor4f(1.0f, 0.22f + 0.1f * alpha, 0.08f, alpha);
        glRasterPos2f(bx, by);
        for (const char *c = msg; *c; c++) glutBitmapCharacter(font, *c);
    }
}


#define MAX_BUTTONS 10
typedef struct {
    float x,y,w,h;
    char  label[64];
    float hoverAnim;  /* 0..1 smooth */
    float pressAnim;
    float wavePhase;  /* for new wave animation on level buttons */
    float orbitAngle; /* for orbit particle effect */
    int   hovered, pressed;
    int   isLevelBtn; /* 1 = apply wave/orbit animation */
} Button;

static Button buttons[MAX_BUTTONS];
static int    buttonCount=0;

static void clearButtons(void){ buttonCount=0; }

static int addButton(float x,float y,float w,float h,const char *label,int isLevel){
    if(buttonCount>=MAX_BUTTONS) return -1;
    Button *b=&buttons[buttonCount];
    b->x=x; b->y=y; b->w=w; b->h=h;
    strncpy(b->label,label,63);
    b->hoverAnim=0; b->pressAnim=0;
    b->wavePhase=(float)(rand()%628)*0.01f;
    b->orbitAngle=(float)(rand()%628)*0.01f;
    b->hovered=0; b->pressed=0;
    b->isLevelBtn=isLevel;
    return buttonCount++;
}

static void updateButtons(int mx,int my){
    for(int i=0;i<buttonCount;i++){
        Button *b=&buttons[i];
        b->hovered=(mx>=b->x&&mx<=b->x+b->w&&my>=b->y&&my<=b->y+b->h);
        float t=b->hovered?1.0f:0.0f;
        b->hoverAnim+=(t-b->hoverAnim)*0.18f;
        if(b->pressAnim>0.0f) b->pressAnim-=0.08f;
        if(b->pressAnim<0.0f) b->pressAnim=0.0f;
        if(b->isLevelBtn) b->orbitAngle+=0.04f;
    }
}

static void drawRoundedRect(float x,float y,float w,float h,float r){
    int segs=10;
    glBegin(GL_POLYGON);
    float cx4[4]={x+r,x+w-r,x+w-r,x+r};
    float cy4[4]={y+r,y+r,y+h-r,y+h-r};
    float sA[4]={3.14159f,3.14159f*1.5f,0.0f,3.14159f*0.5f};
    for(int c=0;c<4;c++)
        for(int s=0;s<=segs;s++){
            float a=sA[c]+(3.14159f*0.5f/segs)*s;
            glVertex2f(cx4[c]+cosf(a)*r,cy4[c]+sinf(a)*r);
        }
    glEnd();
}

/* draw the impressive wave + orbit animation for level buttons */
static void drawLevelButtonFX(Button *b){
    float cx=b->x+b->w*0.5f, cy=b->y+b->h*0.5f;

    /* wave rings */
    float wt=globalTime*3.0f+b->wavePhase;
    for(int ring=0;ring<3;ring++){
        float phase=wt-(float)ring*0.8f;
        float expand=fmodf(phase, 3.14159f*2.0f);
        if(expand<0) expand+=3.14159f*2.0f;
        float normExp=expand/(3.14159f*2.0f); /* 0..1 */
        float rr=b->w*0.55f+normExp*b->w*0.6f;
        float alpha=(1.0f-normExp)*0.22f;
        glColor4f(0.5f,0.75f,1.0f,alpha);
        glLineWidth(2.0f);
        glBegin(GL_LINE_LOOP);
        int segs=32;
        for(int s=0;s<segs;s++){
            float a=2.0f*3.14159f*s/segs;
            glVertex2f(cx+cosf(a)*rr*(b->w/(b->h*2+b->w*0.5f)),
                       cy+sinf(a)*rr*0.5f);
        }
        glEnd();
    }

    /* orbiting sparkle dots */
    int ndots=5;
    float orbitR=b->w*0.54f;
    for(int d=0;d<ndots;d++){
        float a=b->orbitAngle+(float)d*2.0f*3.14159f/ndots;
        float px=cx+cosf(a)*orbitR;
        float py=cy+sinf(a)*orbitR*0.38f;
        float bright=0.55f+0.45f*sinf(a*2+globalTime*4.0f);
        float sz=3.5f+1.5f*sinf(a+globalTime*3.0f);
        glColor4f(0.7f,0.9f,1.0f,bright*0.8f);
        glBegin(GL_QUADS);
        glVertex2f(px-sz,py-sz); glVertex2f(px+sz,py-sz);
        glVertex2f(px+sz,py+sz); glVertex2f(px-sz,py+sz);
        glEnd();
    }

    /* shimmer sweep across button */
    float sweep=fmodf(globalTime*0.9f+b->wavePhase,1.4f)-0.2f;
    float sw=sweep*(b->w+60.0f)-30.0f;
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)b->x,(int)(600-(b->y+b->h)),(int)b->w,(int)b->h);
    glBegin(GL_QUADS);
    glColor4f(1.0f,1.0f,1.0f,0.0f);
    glVertex2f(b->x+sw,      b->y);
    glColor4f(1.0f,1.0f,1.0f,0.12f);
    glVertex2f(b->x+sw+30.0f,b->y);
    glColor4f(1.0f,1.0f,1.0f,0.12f);
    glVertex2f(b->x+sw+30.0f,b->y+b->h);
    glColor4f(1.0f,1.0f,1.0f,0.0f);
    glVertex2f(b->x+sw,      b->y+b->h);
    glEnd();
    glDisable(GL_SCISSOR_TEST);
}

static void drawButton(int idx){
    if(idx<0||idx>=buttonCount) return;
    Button *b=&buttons[idx];
    float lift=b->hoverAnim*5.0f;
    float press=b->pressAnim*3.0f;
    float bx=b->x, by=b->y-lift+press;
    float bw=b->w, bh=b->h;

    if(b->isLevelBtn) drawLevelButtonFX(b);

    /* shadow */
    glColor4f(0.0f,0.0f,0.0f,0.28f+0.15f*b->hoverAnim);
    drawRoundedRect(bx+5,by+6,bw,bh,12.0f);

    /* glow ring when hovered */
    if(b->hoverAnim>0.05f){
        glColor4f(0.4f,0.7f,1.0f,0.18f*b->hoverAnim);
        drawRoundedRect(bx-4,by-4,bw+8,bh+8,16.0f);
    }

    /* gradient fill */
    float t=b->hoverAnim;
    glBegin(GL_QUADS);
    glColor4f(0.22f+0.18f*t,0.44f+0.22f*t,0.82f+0.10f*t,1.0f);
    glVertex2f(bx,by); glVertex2f(bx+bw,by);
    glColor4f(0.14f+0.12f*t,0.32f+0.18f*t,0.72f+0.10f*t,1.0f);
    glVertex2f(bx+bw,by+bh); glVertex2f(bx,by+bh);
    glEnd();

    /* border */
    glColor4f(0.5f+0.4f*t,0.7f+0.25f*t,1.0f,0.7f+0.3f*t);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(bx,by); glVertex2f(bx+bw,by);
    glVertex2f(bx+bw,by+bh); glVertex2f(bx,by+bh);
    glEnd();

    /* label */
    void *font=GLUT_BITMAP_HELVETICA_18;
    int tw=0; for(const char *c=b->label;*c;c++) tw+=glutBitmapWidth(font,*c);
    float tx=bx+(bw-tw)*0.5f, ty=by+bh*0.62f;
    glColor4f(1.0f,1.0f,1.0f,1.0f);
    glRasterPos2f(tx,ty);
    for(const char *c=b->label;*c;c++) glutBitmapCharacter(font,*c);
}

static int buttonHitTest(int idx,int mx,int my){
    if(idx<0||idx>=buttonCount) return 0;
    Button *b=&buttons[idx];
    return(mx>=b->x&&mx<=b->x+b->w&&my>=b->y&&my<=b->y+b->h);
}
static void buttonPress(int idx){
    if(idx>=0&&idx<buttonCount) buttons[idx].pressAnim=1.0f;
}

/* ──────────────────────────────────────────── game state ── */
int hintActive=0;
float winAnim=0.0f;

typedef enum{
    SCREEN_START,
    SCREEN_PLAYING,
    SCREEN_WIN,
    SCREEN_LEADERBOARD
} GameScreen;

typedef struct {
    char  symbol[8];
    int   symbolIdx;
    int   flipped, matched;
    float flipAnim, targetFlipAnim;
    float hoverAnim, targetHoverAnim;
    float bounceAnim;
    float glowAnim;
    float frostedAlpha;   /* NEW: fades IN after match to give frosted effect */
} Card;

static Card cards[MAX_CARDS];

static int level=2, gridRows=4, gridCols=4, cardCount=16;
static int firstCard=-1, secondCard=-1, lockBoard=0;
static int matchedPairs=0, moves=0, hoverCard=-1;
static int winW_dup=INIT_W, winH_dup=INIT_H;  /* unused but kept for compat */

static float cardW=120,cardH=120,gapX=16,gapY=16;
static float startX=0,startY=0,topMargin=140,bottomMargin=105;

static GameScreen currentScreen=SCREEN_START;
static time_t gameStartTime=0;
static int elapsedSeconds=0, bestTime=0, bestMoves=0, lastWinTime=0;
static float bestAccuracy=0.0f;

/* button IDs */
static int btnStart=-1,btnEasy=-1,btnMed=-1,btnHard=-1;
static int btnRestart=-1,btnBack=-1,btnHint=-1;
static int btnPlayAgain=-1,btnChange=-1;
static int btnLeaderboard=-1;
static int btnLbBack=-1;          /* back button on leaderboard screen */
static int mouseX=0,mouseY=0;

/* ──────────────────────────────────────────── helpers ───── */
static void drawText(float x,float y,const char *t,void *f){
    glRasterPos2f(x,y); for(const char *c=t;*c;c++) glutBitmapCharacter(f,*c);
}
static int textWidth(const char *t,void *f){
    int w=0; for(const char *c=t;*c;c++) w+=glutBitmapWidth(f,*c); return w;
}
static void drawCenteredText(float y,const char *t,void *f){
    drawText((float)(winW-textWidth(t,f))*0.5f,y,t,f);
}
static void rect(float x,float y,float w,float h){
    glBegin(GL_QUADS);
    glVertex2f(x,y); glVertex2f(x+w,y);
    glVertex2f(x+w,y-h); glVertex2f(x,y-h);
    glEnd();
}
static void drawGradientBackground(void){
    glBegin(GL_QUADS);
    glColor3f(0.04f,0.05f,0.14f); glVertex2f(0,0);
    glColor3f(0.10f,0.12f,0.24f); glVertex2f((float)winW,0);
    glColor3f(0.03f,0.04f,0.10f); glVertex2f((float)winW,(float)winH);
    glColor3f(0.05f,0.06f,0.12f); glVertex2f(0,(float)winH);
    glEnd();
    glBegin(GL_QUADS);
    glColor4f(0.18f,0.08f,0.36f,0.12f); glVertex2f(200,100);
    glColor4f(0.18f,0.08f,0.36f,0.0f);  glVertex2f(600,100);
    glColor4f(0.18f,0.08f,0.36f,0.0f);  glVertex2f(600,500);
    glColor4f(0.18f,0.08f,0.36f,0.12f); glVertex2f(200,500);
    glEnd();
}
static const char *levelName(int v){
    if(v==1)return"Easy (2x2)"; if(v==2)return"Medium (4x4)"; return"Hard (6x6)";
}
static void applyLevel(void){
    if(level==1){gridRows=2;gridCols=2;}
    else if(level==2){gridRows=4;gridCols=4;}
    else{gridRows=6;gridCols=6;}
    cardCount=gridRows*gridCols;
}
static void computeLayout(void){
    float usableW=(float)winW, usableH=(float)winH-topMargin-bottomMargin;
    float gA=(float)gridCols/(float)gridRows, uA=usableW/usableH;
    if(uA>gA){cardH=(usableH-gapY*(gridRows-1))/gridRows; cardW=cardH;}
    else{cardW=(usableW-gapX*(gridCols-1))/gridCols; cardH=cardW;}
    if(cardW>170)cardW=170; if(cardH>170)cardH=170;
    if(cardW<58)cardW=58;   if(cardH<58)cardH=58;
    float tW=gridCols*cardW+(gridCols-1)*gapX;
    float tH=gridRows*cardH+(gridRows-1)*gapY;
    startX=((float)winW-tW)*0.5f;
    startY=topMargin+((usableH-tH)*0.5f)+tH;
}

void loadLeaderboard() {
    FILE *f = fopen("score.txt", "r");
    if (!f) return;

    playerCount = 0;

    while (fscanf(f, "%s %d %d %f",
        leaderboard[playerCount].name,
        &leaderboard[playerCount].time,
        &leaderboard[playerCount].moves,
        &leaderboard[playerCount].accuracy) == 4) {

        playerCount++;
        if (playerCount >= MAX_PLAYERS) break;
    }

    fclose(f);
}

void saveLeaderboard() {
    FILE *f = fopen("score.txt", "w");
    if (!f) return;

    for (int i = 0; i < playerCount; i++) {
        fprintf(f, "%s %d %d %.2f\n",
            leaderboard[i].name,
            leaderboard[i].time,
            leaderboard[i].moves,
            leaderboard[i].accuracy);
    }

    fclose(f);
}

void sortLeaderboard() {
    for (int i = 0; i < playerCount - 1; i++) {
        for (int j = i + 1; j < playerCount; j++) {
            if (leaderboard[j].time < leaderboard[i].time) {
                Player temp = leaderboard[i];
                leaderboard[i] = leaderboard[j];
                leaderboard[j] = temp;
            }
        }
    }
}

void updateLeaderboard(int time, int moves, float acc) {
    int found = -1;

    for (int i = 0; i < playerCount; i++) {
        if (strcmp(leaderboard[i].name, currentPlayer) == 0) {
            found = i;
            break;
        }
    }

    if (found == -1) {
        /* New player — add to leaderboard */
        if (playerCount < MAX_PLAYERS) {
            strcpy(leaderboard[playerCount].name, currentPlayer);
            leaderboard[playerCount].time = time;
            leaderboard[playerCount].moves = moves;
            leaderboard[playerCount].accuracy = acc;
            playerCount++;
        }
    } else {
        /* Returning player — update only if score is better (lower time) */
        if (time < leaderboard[found].time) {
            leaderboard[found].time = time;
            leaderboard[found].moves = moves;
            leaderboard[found].accuracy = acc;
        }
    }

    sortLeaderboard();
    saveLeaderboard();

    /* Find the current player's rank for highlighting */
    lbHighlightRank = -1;
    for (int i = 0; i < playerCount; i++) {
        if (strcmp(leaderboard[i].name, currentPlayer) == 0) {
            lbHighlightRank = i;
            break;
        }
    }
}

static const char *symbolPool[] = {
    "\xF0\x9F\x8D\x95","\xF0\x9F\x8D\x94","\xF0\x9F\x8D\xA3","\xF0\x9F\x8D\x9C",
    "\xF0\x9F\x8D\xB0","\xF0\x9F\x8D\xA9","\xF0\x9F\x8D\xA6","\xF0\x9F\x8D\x97",
    "\xF0\x9F\x8C\xAE","\xF0\x9F\x8D\xB1","\xF0\x9F\xA5\x90","\xF0\x9F\xA5\x9E",
    "\xF0\x9F\x8D\xB3","\xF0\x9F\xA5\x98","\xF0\x9F\x8D\xB7","\xF0\x9F\x8D\xBF",
    "\xF0\x9F\xA5\x9F","\xF0\x9F\xA7\x86"
};

static void buildDeck(void){
    int pairCount=cardCount/2, idx=0;
    for(int p=0;p<pairCount;p++){
        cards[idx].symbolIdx=p; strcpy(cards[idx++].symbol,symbolPool[p]);
        cards[idx].symbolIdx=p; strcpy(cards[idx++].symbol,symbolPool[p]);
    }
    for(int i=cardCount-1;i>0;i--){
        int j=rand()%(i+1); Card t=cards[i]; cards[i]=cards[j]; cards[j]=t;
    }
    for(int i=0;i<cardCount;i++){
        cards[i].flipped=0; cards[i].matched=0;
        cards[i].flipAnim=0; cards[i].targetFlipAnim=0;
        cards[i].hoverAnim=0; cards[i].targetHoverAnim=0;
        cards[i].bounceAnim=0; cards[i].glowAnim=0;
        cards[i].frostedAlpha=0.0f;
        cardShakeTime[i]=0.0f;
    }
}
static void resetTurnState(void){firstCard=-1;secondCard=-1;lockBoard=0;}
static void unflipTimer(int v){(void)v;
    if(firstCard>=0&&secondCard>=0){
        cards[firstCard].flipped=0; cards[secondCard].flipped=0;
        cards[firstCard].targetFlipAnim=0; cards[secondCard].targetFlipAnim=0;
    }
    resetTurnState();
}
void hintTimer(int v){(void)v; hintActive=0;
    for(int i=0;i<cardCount;i++) if(!cards[i].matched){cards[i].flipped=0;cards[i].targetFlipAnim=0;}
}
static void finishWin(void){
    float acc = (moves>0)?((float)matchedPairs/(float)moves)*100.0f:0.0f;
    lastWinTime = elapsedSeconds;

    updateLeaderboard(lastWinTime, moves, acc);

    winAnim = 0.0f;
    currentScreen = SCREEN_WIN;
}
static void getCardRect(int idx,float *x,float *y,float *w,float *h){
    int r=idx/gridCols, c=idx%gridCols;
    *w=cardW; *h=cardH;
    *x=startX+c*(cardW+gapX);
    *y=startY-r*(cardH+gapY);
}
static void checkMatch(void){
    int isMatch=strcmp(cards[firstCard].symbol,cards[secondCard].symbol)==0;
    if(isMatch){
        int r1=firstCard/gridCols, c1=firstCard%gridCols;
        int r2=secondCard/gridCols, c2=secondCard%gridCols;
        float cx1=startX+c1*(cardW+gapX)+cardW*0.5f;
        float cy1=startY-r1*(cardH+gapY)-cardH*0.5f;
        float cx2=startX+c2*(cardW+gapX)+cardW*0.5f;
        float cy2=startY-r2*(cardH+gapY)-cardH*0.5f;

        /* ── CORRECT effects ── */
        playCorrectSound();
        triggerFlash(0.05f, 0.85f, 0.30f, 0.38f, 0.018f); /* green flash */

        /* gold + green particles */
        spawnParticles(cx1,cy1, 1.0f,0.85f,0.1f);
        spawnParticles(cx1,cy1, 0.2f,1.0f,0.4f);
        spawnParticles(cx2,cy2, 1.0f,0.85f,0.1f);
        spawnParticles(cx2,cy2, 0.2f,1.0f,0.4f);

        /* "PERFECT!" banners */
        spawnBanner(cx1, cy1);
        spawnBanner(cx2, cy2 - 30.0f);

        cards[firstCard].matched=1;  cards[secondCard].matched=1;
        cards[firstCard].bounceAnim=1.0f; cards[secondCard].bounceAnim=1.0f;
        cards[firstCard].glowAnim=1.0f;   cards[secondCard].glowAnim=1.0f;
        matchedPairs++;
        if(matchedPairs==cardCount/2) finishWin();
        resetTurnState();
    } else {
        /* ── INCORRECT effects ── */
        playWrongSound();
        triggerFlash(0.90f, 0.05f, 0.05f, 0.42f, 0.022f); /* red flash */

        /* red-orange particle splatter */
        float cx1=startX+(firstCard%gridCols)*(cardW+gapX)+cardW*0.5f;
        float cy1=startY-(firstCard/gridCols)*(cardH+gapY)-cardH*0.5f;
        float cx2=startX+(secondCard%gridCols)*(cardW+gapX)+cardW*0.5f;
        float cy2=startY-(secondCard/gridCols)*(cardH+gapY)-cardH*0.5f;
        spawnParticles(cx1,cy1, 1.0f,0.20f,0.05f);
        spawnParticles(cx2,cy2, 1.0f,0.20f,0.05f);

        /* "NOPE!" banner between the two cards */
        float midX=(cx1+cx2)*0.5f, midY=(cy1+cy2)*0.5f;
        spawnErrBanner(midX, midY);

        /* violent shake on both wrong cards */
        startCardShake(firstCard);
        startCardShake(secondCard);

        lockBoard=1;
        glutTimerFunc(750,unflipTimer,0);
    }
}
static void flipCard(int idx){
    if(currentScreen!=SCREEN_PLAYING)return;
    if(lockBoard||idx<0||idx>=cardCount)return;
    if(idx==firstCard||cards[idx].matched)return;
    cards[idx].flipped=1; cards[idx].targetFlipAnim=1.0f;
    if(firstCard==-1)firstCard=idx;
    else{secondCard=idx; moves++; checkMatch();}
}

/* ──────────────────────────────────────────── card drawing ─ */
static void drawCardFace(float x,float y,float w,float h,int idx,float reveal){
    if(reveal<0.5f){
        /* BACK FACE */
        glBegin(GL_QUADS);
        glColor3f(0.14f,0.28f,0.62f); glVertex2f(x,y);
        glColor3f(0.18f,0.36f,0.78f); glVertex2f(x+w,y);
        glColor3f(0.10f,0.20f,0.52f); glVertex2f(x+w,y-h);
        glColor3f(0.12f,0.22f,0.56f); glVertex2f(x,y-h);
        glEnd();
        glColor4f(0.38f,0.60f,0.98f,0.45f);
        glLineWidth(1.5f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(x+8,y-8); glVertex2f(x+w-8,y-8);
        glVertex2f(x+w-8,y-h+8); glVertex2f(x+8,y-h+8);
        glEnd();
        glColor3f(0.70f,0.82f,1.0f);
        float qw=(float)glutBitmapWidth(GLUT_BITMAP_TIMES_ROMAN_24,'?');
        drawText(x+(w-qw)*0.5f,y-h*0.52f,"?",GLUT_BITMAP_TIMES_ROMAN_24);
    } else {
        /* FRONT FACE */
        int si=cards[idx].symbolIdx;
        float gf=0.5f+0.5f*sinf(globalTime*5.0f);

        if(cards[idx].matched){
            /* soft green bg for matched */
            glBegin(GL_QUADS);
            glColor3f(0.20f,0.65f+0.05f*gf,0.36f); glVertex2f(x,y);
            glColor3f(0.24f,0.75f,0.42f);           glVertex2f(x+w,y);
            glColor3f(0.16f,0.55f,0.30f);           glVertex2f(x+w,y-h);
            glColor3f(0.18f,0.58f,0.32f);           glVertex2f(x,y-h);
            glEnd();
        } else {
            /* cream bg */
            glBegin(GL_QUADS);
            glColor3f(0.98f,0.92f,0.72f); glVertex2f(x,y);
            glColor3f(1.00f,0.97f,0.80f); glVertex2f(x+w,y);
            glColor3f(0.94f,0.87f,0.64f); glVertex2f(x+w,y-h);
            glColor3f(0.96f,0.88f,0.68f); glVertex2f(x,y-h);
            glEnd();
        }

        /* ── CUISINE IMAGE ── */
        float pad=w*0.10f;
        float imgX=x+pad, imgY=y-pad;
        float imgW=w-pad*2, imgH=h*0.72f;
        if(texLoaded[si]){
            drawTexturedQuad(imgX,imgY,imgW,imgH,si,1.0f);
        } else {
            /* fallback: coloured placeholder + label */
            glColor4f(0.5f,0.6f,0.8f,0.35f);
            rect(imgX,imgY,imgW,imgH);
        }

        /* inner frame */
        glColor4f(0.60f,0.40f,0.10f,0.25f);
        glLineWidth(1.5f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(x+5,y-5); glVertex2f(x+w-5,y-5);
        glVertex2f(x+w-5,y-h+5); glVertex2f(x+5,y-h+5);
        glEnd();

        /* cuisine name below image */
        void *font=(w>110)?GLUT_BITMAP_HELVETICA_12:GLUT_BITMAP_8_BY_13;
        const char *lbl=symbolLabel[si];
        int lw=textWidth(lbl,font);
        float lx=x+(w-lw)*0.5f;
        float ly=y-h*0.88f;
        if(cards[idx].matched) glColor3f(0.94f,1.0f,0.92f);
        else                   glColor3f(0.16f,0.08f,0.02f);
        drawText(lx,ly,lbl,font);

        /* ── FROSTED OVERLAY for matched cards ── */
        if(cards[idx].matched && cards[idx].frostedAlpha>0.01f){
            float fa=cards[idx].frostedAlpha*0.52f;
            glColor4f(0.12f,0.55f,0.28f,fa);
            rect(x,y,w,h);
            /* checkmark */
            glColor4f(1.0f,1.0f,1.0f,cards[idx].frostedAlpha*0.9f);
            glLineWidth(3.5f);
            glBegin(GL_LINE_STRIP);
            float cx2=x+w*0.5f, cy2=y-h*0.5f;
            float cs=w*0.18f;
            glVertex2f(cx2-cs,cy2);
            glVertex2f(cx2-cs*0.3f,cy2+cs*0.8f);
            glVertex2f(cx2+cs*0.9f,cy2-cs*0.7f);
            glEnd();
        }
    }

    /* outer border */
    if(idx==hoverCard&&!cards[idx].matched&&currentScreen==SCREEN_PLAYING)
        glColor3f(1.0f,1.0f,1.0f);
    else if(cards[idx].matched)
        glColor4f(0.3f,1.0f,0.5f,0.8f);
    else
        glColor3f(0.08f,0.10f,0.18f);
    glLineWidth(2.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x,y); glVertex2f(x+w,y); glVertex2f(x+w,y-h); glVertex2f(x,y-h);
    glEnd();
}

static void drawCard(int idx){
    float x,y,w,h; getCardRect(idx,&x,&y,&w,&h);

    /* shake offset for wrong-match animation */
    float shakeOff = getCardShakeOffset(idx);
    x += shakeOff;

    float flipScale=fabsf(cosf(cards[idx].flipAnim*3.14159f));
    if(flipScale<0.08f)flipScale=0.08f;
    float hoverScale=1.0f+0.09f*cards[idx].hoverAnim;
    float bounceScale=1.0f+0.18f*sinf(cards[idx].bounceAnim*3.14159f);

    glColor4f(0,0,0,0.22f); rect(x+7,y-7,w,h);
    glPushMatrix();
    glTranslatef(x+w*0.5f,y-h*0.5f,0);
    glScalef(flipScale*hoverScale*bounceScale,hoverScale*bounceScale,1);
    glTranslatef(-(x+w*0.5f),-(y-h*0.5f),0);
    drawCardFace(x,y,w,h,idx,cards[idx].flipAnim);
    glPopMatrix();
}

/* ──────────────────────────────────────────── progress bar ─ */
static void drawProgressBar(void){
    int total=cardCount/2;
    float pct=(total>0)?(float)matchedPairs/(float)total:0.0f;
    float bx=60,by=120,bw=(float)winW-120,bh=10;
    glColor4f(0.1f,0.1f,0.2f,0.7f); rect(bx,by,bw,bh);
    float fillW=bw*pct;
    glBegin(GL_QUADS);
    glColor4f(0.2f,0.8f,0.5f,0.9f); glVertex2f(bx,by);
    glColor4f(0.4f,1.0f,0.6f,0.9f); glVertex2f(bx+fillW,by);
    glColor4f(0.2f,0.7f,0.4f,0.9f); glVertex2f(bx+fillW,by-bh);
    glColor4f(0.1f,0.6f,0.3f,0.9f); glVertex2f(bx,by-bh);
    glEnd();
    glColor4f(0.5f,1.0f,0.7f,0.4f); glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(bx,by); glVertex2f(bx+bw,by);
    glVertex2f(bx+bw,by-bh); glVertex2f(bx,by-bh);
    glEnd();
    char pbuf[32]; snprintf(pbuf,sizeof(pbuf),"%d / %d pairs",matchedPairs,total);
    glColor4f(0.7f,1.0f,0.8f,0.8f);
    drawCenteredText(by-bh-4,pbuf,GLUT_BITMAP_HELVETICA_12);
}

/* ──────────────────────────────────────────── screens ────── */

static void rebuildStartButtons(void){
    clearButtons();
    float cx=(float)winW*0.5f;
    btnEasy =addButton(cx-270,310,160,48,"Easy (2x2)",1);
    btnMed  =addButton(cx- 70,310,160,48,"Medium (4x4)",1);
    btnHard =addButton(cx+130,310,160,48,"Hard (6x6)",1);
    btnStart=addButton(cx- 80,380,180,50,"START",0);
    btnLeaderboard=addButton(cx-100,450,220,44,"Leaderboard",0);
}

static void rebuildPlayButtons(void){
    clearButtons();
    float bx=(float)winW-500;
    float by=(float)winH-80;
    btnHint       = addButton(bx,      by, 110, 38, "H: Hint",     0);
    btnRestart    = addButton(bx+120,  by, 120, 38, "R: Restart",  0);
    btnBack       = addButton(bx+250,  by, 110, 38, "B: Back",     0);
    btnLeaderboard= addButton(bx+370,  by, 140, 38, "Leaderboard", 0);
}

static void rebuildWinButtons(void){
    clearButtons();
    float ctrX=(float)winW*0.5f;
    btnPlayAgain  = addButton(ctrX-320, 380, 180, 50, "Play Again",     0);
    btnChange     = addButton(ctrX- 90, 380, 180, 50, "Change Level",   0);
    btnLeaderboard= addButton(ctrX+140, 380, 180, 50, "Leaderboard",    0);
}

static void rebuildLeaderboardButtons(void){
    clearButtons();
    float cx=(float)winW*0.5f;
    btnLbBack = addButton(cx-80, (float)winH-80, 160, 44, "B: Back", 0);
}

static void drawStartScreen(void){
    char info[128];
    drawGradientBackground(); drawStars();
    float ts=1.0f+0.04f*sinf(globalTime*2.0f);
    glPushMatrix();
    glTranslatef((float)winW*0.5f,160,0); glScalef(ts,ts,1); glTranslatef(-(float)winW*0.5f,-160,0);
    glColor3f(1.0f,0.92f,0.50f);
    drawCenteredText(160.0f,"Memory Match - Cuisine Edition",GLUT_BITMAP_TIMES_ROMAN_24);
    glPopMatrix();
    glColor3f(0.74f,0.84f,0.96f);
    drawCenteredText(210.0f,"Match the cuisine pairs!",GLUT_BITMAP_HELVETICA_18);
    snprintf(info,sizeof(info),"Selected Level: %s",levelName(level));
    glColor3f(1.0f,0.88f,0.45f);
    drawCenteredText(270.0f,info,GLUT_BITMAP_HELVETICA_18);

    /* Show current player name */
    if (strlen(currentPlayer) > 0) {
        char pinfo[100];
        snprintf(pinfo, sizeof(pinfo), "Player: %s", currentPlayer);
        glColor3f(0.5f, 0.9f, 0.7f);
        drawCenteredText(240.0f, pinfo, GLUT_BITMAP_HELVETICA_12);
    }

    rebuildStartButtons();
    if(level==1)buttons[btnEasy].hoverAnim=0.8f;
    else if(level==2)buttons[btnMed].hoverAnim=0.8f;
    else buttons[btnHard].hoverAnim=0.8f;
    updateButtons(mouseX,mouseY);
    for(int i=0;i<buttonCount;i++) drawButton(i);
    glColor3f(0.60f,0.70f,0.88f);
    drawCenteredText(520.0f,"-- Best Records --",GLUT_BITMAP_HELVETICA_18);
    snprintf(info,sizeof(info),"Time: %ds   Moves: %d   Accuracy: %.1f%%",bestTime,bestMoves,bestAccuracy);
    drawCenteredText(550.0f,info,GLUT_BITMAP_HELVETICA_12);
    glColor3f(0.45f,0.52f,0.70f);
    drawCenteredText(580.0f,"ESC to quit",GLUT_BITMAP_HELVETICA_12);
}

static void drawPlayingScreen(void){
    char line[160];
    float accuracy=(moves>0)?((float)matchedPairs/(float)moves)*100.0f:0.0f;
    drawGradientBackground(); drawStars();
    glColor3f(0.92f,0.95f,1.0f);
    drawCenteredText(36.0f,"Memory Match",GLUT_BITMAP_TIMES_ROMAN_24);
    snprintf(line,sizeof(line),"Level: %s",levelName(level));
    glColor3f(0.78f,0.85f,0.97f);
    drawCenteredText(62.0f,line,GLUT_BITMAP_HELVETICA_18);
    snprintf(line,sizeof(line),"Time: %ds   Moves: %d   Accuracy: %.1f%%",elapsedSeconds,moves,accuracy);
    drawCenteredText(86.0f,line,GLUT_BITMAP_HELVETICA_18);
    drawProgressBar();
    for(int i=0;i<cardCount;i++) drawCard(i);
    drawParticles();
    drawBanners();
    drawErrBanners();
    drawFlash();          /* flash OVER everything else */
    rebuildPlayButtons();
    updateButtons(mouseX,mouseY);
    for(int i=0;i<buttonCount;i++) drawButton(i);
    glColor3f(0.50f,0.58f,0.76f);
    drawCenteredText((float)winH-20.0f,"Click cards to flip",GLUT_BITMAP_HELVETICA_12);
}

static void drawWinScreen(void){
    char line[180];
    float accuracy=(moves>0)?((float)matchedPairs/(float)moves)*100.0f:0.0f;
    drawGradientBackground(); drawStars(); drawParticles();
    if(rand()%4==0){
        float rx=(float)(rand()%winW), ry=50.0f+(rand()%200);
        spawnParticles(rx,ry,0.6f+(rand()%40)*0.01f,0.6f+(rand()%40)*0.01f,0.2f+(rand()%60)*0.01f);
    }
    float scale=1.0f+0.12f*sinf(winAnim*3.14f*2.0f);
    glPushMatrix();
    glTranslatef((float)winW*0.5f,190,0); glScalef(scale,scale,1); glTranslatef(-(float)winW*0.5f,-190,0);
    glColor3f(1.0f,0.92f+0.05f*sinf(winAnim*4.0f),0.30f);
    drawCenteredText(190.0f,"YOU WIN!",GLUT_BITMAP_TIMES_ROMAN_24);
    glPopMatrix();
    glColor3f(0.82f,0.88f,0.98f);
    snprintf(line,sizeof(line),"Level: %s",levelName(level));
    drawCenteredText(250.0f,line,GLUT_BITMAP_HELVETICA_18);
    snprintf(line,sizeof(line),"Time: %ds   Moves: %d   Accuracy: %.1f%%",lastWinTime,moves,accuracy);
    drawCenteredText(282.0f,line,GLUT_BITMAP_HELVETICA_18);
    snprintf(line,sizeof(line),"Best  Time: %ds   Best Moves: %d   Best Accuracy: %.1f%%",bestTime,bestMoves,bestAccuracy);
    glColor3f(1.0f,0.88f,0.42f);
    drawCenteredText(314.0f,line,GLUT_BITMAP_HELVETICA_18);
    rebuildWinButtons();
    updateButtons(mouseX,mouseY);
    for(int i=0;i<buttonCount;i++) drawButton(i);
}

/* ═══════════════════════════════════════════════════════════
 *         DRAMATIC LEADERBOARD SCREEN
 * ═══════════════════════════════════════════════════════════ */
static void drawLeaderboardScreen(void){
    drawGradientBackground();
    drawStars();
    drawParticles();

    /* ── Animated firework bursts ── */
    lbFireworkTimer += 0.016f;
    if (lbFireworkTimer > 0.6f) {
        lbFireworkTimer = 0.0f;
        float fx = 100.0f + (float)(rand() % (winW - 200));
        float fy = 50.0f  + (float)(rand() % 150);
        spawnFirework(fx, fy);
    }

    /* ── Pulsating & rotating crown / trophy at top ── */
    float crownPulse = 1.0f + 0.15f * sinf(globalTime * 3.0f);
    float crownY = 60.0f + 5.0f * sinf(globalTime * 1.5f);   /* float up/down */

    /* Draw a large glowing circle behind the title */
    {
        float cx = (float)winW * 0.5f;
        float cy = crownY + 10.0f;
        float radius = 55.0f * crownPulse;

        /* Outer glow (multiple layers for diffusion) */
        for (int layer = 3; layer >= 0; layer--) {
            float lr = radius + layer * 15.0f;
            float la = 0.06f * (4 - layer);
            glColor4f(1.0f, 0.85f, 0.2f, la);
            glBegin(GL_TRIANGLE_FAN);
            glVertex2f(cx, cy);
            for (int s = 0; s <= 36; s++) {
                float a = 2.0f * 3.14159f * s / 36.0f;
                glVertex2f(cx + cosf(a) * lr, cy + sinf(a) * lr);
            }
            glEnd();
        }

        /* Inner solid circle */
        glColor4f(1.0f, 0.75f, 0.0f, 0.35f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int s = 0; s <= 36; s++) {
            float a = 2.0f * 3.14159f * s / 36.0f;
            glVertex2f(cx + cosf(a) * radius, cy + sinf(a) * radius);
        }
        glEnd();
    }

    /* Title with dramatic scaling */
    {
        float ts = 1.0f + 0.06f * sinf(globalTime * 2.5f);
        glPushMatrix();
        glTranslatef((float)winW * 0.5f, crownY, 0);
        glScalef(ts, ts, 1);
        glTranslatef(-(float)winW * 0.5f, -crownY, 0);

        /* Shadow */
        glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
        drawCenteredText(crownY + 3, "LEADERBOARD", GLUT_BITMAP_TIMES_ROMAN_24);

        /* Gold gradient text */
        float goldPulse = 0.85f + 0.15f * sinf(globalTime * 4.0f);
        glColor3f(1.0f * goldPulse, 0.85f * goldPulse, 0.2f);
        drawCenteredText(crownY, "LEADERBOARD", GLUT_BITMAP_TIMES_ROMAN_24);

        glPopMatrix();
    }

    /* ── Subtitle with player count ── */
    {
        char sub[64];
        snprintf(sub, sizeof(sub), "Top Champions - %d players", playerCount);
        glColor4f(0.6f, 0.75f, 1.0f, 0.7f + 0.3f * sinf(globalTime * 2.0f));
        drawCenteredText(crownY + 30, sub, GLUT_BITMAP_HELVETICA_12);
    }

    /* ── Leaderboard rows with cascade reveal ── */
    float rowStartY = 140.0f;
    float rowHeight = 48.0f;
    float rowWidth  = 650.0f;
    float rowLeft   = ((float)winW - rowWidth) * 0.5f;
    int showCount   = (playerCount < 10) ? playerCount : 10;

    for (int i = 0; i < showCount; i++) {
        /* Cascade reveal: each row appears with a delay */
        float revealDelay = (float)i * 0.15f;
        float revealT = lbRevealTime - revealDelay;
        if (revealT < 0.0f) continue;  /* not yet visible */
        if (revealT > 1.0f) revealT = 1.0f;

        /* Ease-out animation */
        float easeT = 1.0f - (1.0f - revealT) * (1.0f - revealT);
        float slideX = (1.0f - easeT) * 300.0f;   /* slides in from right */
        float alpha  = easeT;

        float ry = rowStartY + i * rowHeight;
        float rx = rowLeft + slideX;

        /* Floating sine animation per row */
        float floatOffset = sinf(globalTime * 2.0f + i * 0.8f) * 3.0f;
        ry += floatOffset;

        int isCurrentPlayer = (strcmp(leaderboard[i].name, currentPlayer) == 0);

        /* ── Row background ── */
        if (i == 0) {
            /* 1st place: Gold row with pulsating glow */
            float glow = 0.12f + 0.08f * sinf(globalTime * 5.0f);

            /* Outer glow */
            glColor4f(1.0f, 0.85f, 0.0f, glow * alpha);
            drawRoundedRect(rx - 6, ry - 6, rowWidth + 12, rowHeight - 4 + 12, 14.0f);

            /* Gold gradient */
            glBegin(GL_QUADS);
            glColor4f(0.35f, 0.28f, 0.05f, 0.9f * alpha); glVertex2f(rx, ry);
            glColor4f(0.45f, 0.35f, 0.08f, 0.9f * alpha); glVertex2f(rx + rowWidth, ry);
            glColor4f(0.30f, 0.22f, 0.03f, 0.9f * alpha); glVertex2f(rx + rowWidth, ry + rowHeight - 4);
            glColor4f(0.25f, 0.18f, 0.02f, 0.9f * alpha); glVertex2f(rx, ry + rowHeight - 4);
            glEnd();
        } else if (i == 1) {
            /* 2nd place: Silver */
            glBegin(GL_QUADS);
            glColor4f(0.25f, 0.25f, 0.30f, 0.85f * alpha); glVertex2f(rx, ry);
            glColor4f(0.30f, 0.30f, 0.35f, 0.85f * alpha); glVertex2f(rx + rowWidth, ry);
            glColor4f(0.20f, 0.20f, 0.25f, 0.85f * alpha); glVertex2f(rx + rowWidth, ry + rowHeight - 4);
            glColor4f(0.18f, 0.18f, 0.22f, 0.85f * alpha); glVertex2f(rx, ry + rowHeight - 4);
            glEnd();
        } else if (i == 2) {
            /* 3rd place: Bronze */
            glBegin(GL_QUADS);
            glColor4f(0.30f, 0.18f, 0.08f, 0.85f * alpha); glVertex2f(rx, ry);
            glColor4f(0.35f, 0.22f, 0.10f, 0.85f * alpha); glVertex2f(rx + rowWidth, ry);
            glColor4f(0.25f, 0.15f, 0.06f, 0.85f * alpha); glVertex2f(rx + rowWidth, ry + rowHeight - 4);
            glColor4f(0.22f, 0.12f, 0.05f, 0.85f * alpha); glVertex2f(rx, ry + rowHeight - 4);
            glEnd();
        } else {
            /* Other rows: Dark translucent */
            glColor4f(0.10f, 0.12f, 0.22f, 0.75f * alpha);
            drawRoundedRect(rx, ry, rowWidth, rowHeight - 4, 8.0f);
        }

        /* Current player highlight (bright pulsing border) */
        if (isCurrentPlayer) {
            float hlAlpha = 0.5f + 0.5f * sinf(globalTime * 6.0f);
            glColor4f(0.3f, 1.0f, 0.5f, hlAlpha * alpha);
            glLineWidth(3.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(rx, ry);
            glVertex2f(rx + rowWidth, ry);
            glVertex2f(rx + rowWidth, ry + rowHeight - 4);
            glVertex2f(rx, ry + rowHeight - 4);
            glEnd();
        }

        /* ── Row border ── */
        if (i < 3) {
            glColor4f(0.6f + 0.3f * sinf(globalTime * 3.0f + i),
                      0.5f + 0.2f * sinf(globalTime * 2.5f + i),
                      0.2f, 0.5f * alpha);
        } else {
            glColor4f(0.3f, 0.4f, 0.6f, 0.3f * alpha);
        }
        glLineWidth(1.5f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(rx, ry);
        glVertex2f(rx + rowWidth, ry);
        glVertex2f(rx + rowWidth, ry + rowHeight - 4);
        glVertex2f(rx, ry + rowHeight - 4);
        glEnd();

        /* ── Rank medal/badge ── */
        {
            float medalX = rx + 28.0f;
            float medalY = ry + rowHeight * 0.5f;
            float medalR = 14.0f;

            if (i == 0) {
                /* Gold medal */
                float pulse = 1.0f + 0.1f * sinf(globalTime * 5.0f);
                glColor4f(1.0f, 0.85f, 0.0f, alpha);
                glBegin(GL_TRIANGLE_FAN);
                glVertex2f(medalX, medalY);
                for (int s = 0; s <= 20; s++) {
                    float a = 2.0f * 3.14159f * s / 20.0f;
                    glVertex2f(medalX + cosf(a) * medalR * pulse, medalY + sinf(a) * medalR * pulse);
                }
                glEnd();
                /* "1" text */
                glColor4f(0.1f, 0.05f, 0.0f, alpha);
                float tw1 = (float)glutBitmapWidth(GLUT_BITMAP_HELVETICA_18, '1');
                glRasterPos2f(medalX - tw1 * 0.5f, medalY + 5);
                glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, '1');
            } else if (i == 1) {
                /* Silver medal */
                glColor4f(0.78f, 0.78f, 0.85f, alpha);
                glBegin(GL_TRIANGLE_FAN);
                glVertex2f(medalX, medalY);
                for (int s = 0; s <= 20; s++) {
                    float a = 2.0f * 3.14159f * s / 20.0f;
                    glVertex2f(medalX + cosf(a) * medalR, medalY + sinf(a) * medalR);
                }
                glEnd();
                glColor4f(0.1f, 0.1f, 0.15f, alpha);
                float tw2 = (float)glutBitmapWidth(GLUT_BITMAP_HELVETICA_18, '2');
                glRasterPos2f(medalX - tw2 * 0.5f, medalY + 5);
                glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, '2');
            } else if (i == 2) {
                /* Bronze medal */
                glColor4f(0.80f, 0.50f, 0.20f, alpha);
                glBegin(GL_TRIANGLE_FAN);
                glVertex2f(medalX, medalY);
                for (int s = 0; s <= 20; s++) {
                    float a = 2.0f * 3.14159f * s / 20.0f;
                    glVertex2f(medalX + cosf(a) * medalR, medalY + sinf(a) * medalR);
                }
                glEnd();
                glColor4f(0.1f, 0.05f, 0.0f, alpha);
                float tw3 = (float)glutBitmapWidth(GLUT_BITMAP_HELVETICA_18, '3');
                glRasterPos2f(medalX - tw3 * 0.5f, medalY + 5);
                glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, '3');
            } else {
                /* Number badge */
                glColor4f(0.3f, 0.4f, 0.6f, 0.6f * alpha);
                glBegin(GL_TRIANGLE_FAN);
                glVertex2f(medalX, medalY);
                for (int s = 0; s <= 20; s++) {
                    float a = 2.0f * 3.14159f * s / 20.0f;
                    glVertex2f(medalX + cosf(a) * 12.0f, medalY + sinf(a) * 12.0f);
                }
                glEnd();
                char numStr[4];
                snprintf(numStr, sizeof(numStr), "%d", i + 1);
                glColor4f(0.8f, 0.9f, 1.0f, alpha);
                float twr = (float)textWidth(numStr, GLUT_BITMAP_HELVETICA_12);
                glRasterPos2f(medalX - twr * 0.5f, medalY + 4);
                for (const char *c = numStr; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
            }
        }

        /* ── Player info text ── */
        {
            char rowText[200];
            snprintf(rowText, sizeof(rowText), "%s", leaderboard[i].name);

            /* Name - larger, colored by rank */
            if (i == 0) glColor4f(1.0f, 0.92f, 0.3f, alpha);
            else if (i == 1) glColor4f(0.85f, 0.85f, 0.95f, alpha);
            else if (i == 2) glColor4f(0.90f, 0.65f, 0.30f, alpha);
            else if (isCurrentPlayer) glColor4f(0.4f, 1.0f, 0.6f, alpha);
            else glColor4f(0.85f, 0.90f, 1.0f, alpha);

            drawText(rx + 55, ry + rowHeight * 0.62f, rowText, GLUT_BITMAP_HELVETICA_18);

            /* Stats - smaller, to the right */
            char stats[128];
            snprintf(stats, sizeof(stats), "Time: %ds  |  Moves: %d  |  Accuracy: %.1f%%",
                leaderboard[i].time, leaderboard[i].moves, leaderboard[i].accuracy);
            glColor4f(0.6f, 0.7f, 0.85f, 0.8f * alpha);
            drawText(rx + 220, ry + rowHeight * 0.62f, stats, GLUT_BITMAP_HELVETICA_12);
        }

        /* ── Shimmer sweep effect on top 3 rows ── */
        if (i < 3 && easeT > 0.8f) {
            float sweep = fmodf(globalTime * 0.6f + i * 0.5f, 2.5f) - 0.5f;
            float sw2 = sweep * (rowWidth + 80.0f) - 40.0f;
            glBegin(GL_QUADS);
            glColor4f(1.0f, 1.0f, 1.0f, 0.0f);
            glVertex2f(rx + sw2, ry);
            glColor4f(1.0f, 1.0f, 1.0f, 0.08f * alpha);
            glVertex2f(rx + sw2 + 40.0f, ry);
            glColor4f(1.0f, 1.0f, 1.0f, 0.08f * alpha);
            glVertex2f(rx + sw2 + 40.0f, ry + rowHeight - 4);
            glColor4f(1.0f, 1.0f, 1.0f, 0.0f);
            glVertex2f(rx + sw2, ry + rowHeight - 4);
            glEnd();
        }
    }

    /* ── "No entries yet" message ── */
    if (playerCount == 0) {
        float pulse = 0.5f + 0.5f * sinf(globalTime * 2.0f);
        glColor4f(0.6f, 0.7f, 0.9f, pulse);
        drawCenteredText(300, "No scores yet! Play a game to claim the throne!", GLUT_BITMAP_HELVETICA_18);
    }

    /* ── Bottom decorative line ── */
    {
        float lineY = rowStartY + showCount * rowHeight + 20.0f;
        float lineAlpha = 0.3f + 0.15f * sinf(globalTime * 2.0f);
        glBegin(GL_QUADS);
        glColor4f(1.0f, 0.85f, 0.2f, 0.0f);
        glVertex2f(rowLeft, lineY);
        glColor4f(1.0f, 0.85f, 0.2f, lineAlpha);
        glVertex2f(rowLeft + rowWidth * 0.5f, lineY);
        glColor4f(1.0f, 0.85f, 0.2f, lineAlpha);
        glVertex2f(rowLeft + rowWidth * 0.5f, lineY + 2);
        glColor4f(1.0f, 0.85f, 0.2f, 0.0f);
        glVertex2f(rowLeft, lineY + 2);
        glEnd();
        glBegin(GL_QUADS);
        glColor4f(1.0f, 0.85f, 0.2f, lineAlpha);
        glVertex2f(rowLeft + rowWidth * 0.5f, lineY);
        glColor4f(1.0f, 0.85f, 0.2f, 0.0f);
        glVertex2f(rowLeft + rowWidth, lineY);
        glColor4f(1.0f, 0.85f, 0.2f, 0.0f);
        glVertex2f(rowLeft + rowWidth, lineY + 2);
        glColor4f(1.0f, 0.85f, 0.2f, lineAlpha);
        glVertex2f(rowLeft + rowWidth * 0.5f, lineY + 2);
        glEnd();
    }

    /* ── Back button ── */
    rebuildLeaderboardButtons();
    updateButtons(mouseX, mouseY);
    for (int i = 0; i < buttonCount; i++) drawButton(i);

    /* ── Help text ── */
    glColor4f(0.5f, 0.6f, 0.8f, 0.6f + 0.2f * sinf(globalTime * 1.5f));
    drawCenteredText((float)winH - 30, "Press B to go back", GLUT_BITMAP_HELVETICA_12);
}

void printLeaderboard() {
    printf("\n\n===== LEADERBOARD =====\n");

    for (int i = 0; i < playerCount; i++) {
        printf("%d. %s | Time: %ds | Moves: %d | Accuracy: %.2f%%\n",
            i + 1,
            leaderboard[i].name,
            leaderboard[i].time,
            leaderboard[i].moves,
            leaderboard[i].accuracy
        );
    }

    printf("================================\n\n");
}

/* ──────────────────────────────────────────── flow ───────── */
static void enterLeaderboard(void) {
    lbRevealTime = 0.0f;
    lbFireworkTimer = 0.0f;

    /* Find current player rank */
    lbHighlightRank = -1;
    for (int i = 0; i < playerCount; i++) {
        if (strcmp(leaderboard[i].name, currentPlayer) == 0) {
            lbHighlightRank = i;
            break;
        }
    }

    currentScreen = SCREEN_LEADERBOARD;
}

static void startGame(void){
    applyLevel(); computeLayout(); buildDeck();
    resetTurnState(); matchedPairs=0; moves=0;
    hoverCard=-1; elapsedSeconds=0;
    gameStartTime=time(NULL);
    particleCount=0;
    memset(banners,   0, sizeof(banners));
    memset(errBanners,0, sizeof(errBanners));
    memset(cardShakeTime,0,sizeof(cardShakeTime));
    screenFlash.alpha=0.0f;
    currentScreen=SCREEN_PLAYING;
}
static void goToStartScreen(void){ currentScreen=SCREEN_START; }

/* ──────────────────────────────────────────── callbacks ──── */
static void display(void){
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();

    if(currentScreen == SCREEN_START)
        drawStartScreen();
    else if(currentScreen == SCREEN_PLAYING)
        drawPlayingScreen();
    else if(currentScreen == SCREEN_WIN)
        drawWinScreen();
    else if(currentScreen == SCREEN_LEADERBOARD)
        drawLeaderboardScreen();

    glutSwapBuffers();
}

static void reshape(int w,int h){
    winW=(w<=0)?INIT_W:w; winH=(h<=0)?INIT_H:h;
    computeLayout();
    glViewport(0,0,winW,winH);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluOrtho2D(0,winW,winH,0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}

static int cardIndexFromMouse(int mx,int my){
    for(int i=0;i<cardCount;i++){
        float x,y,w,h; getCardRect(i,&x,&y,&w,&h);
        if(mx>=x&&mx<=x+w&&my<=y&&my>=y-h) return i;
    }
    return -1;
}

static void mouse(int button,int state,int x,int y){
    mouseX=x; mouseY=y;

    if(button==GLUT_LEFT_BUTTON && state==GLUT_DOWN){

        if(currentScreen==SCREEN_START){
            if(buttonHitTest(btnEasy,x,y)){level=1;buttonPress(btnEasy);}
            else if(buttonHitTest(btnMed,x,y)){level=2;buttonPress(btnMed);}
            else if(buttonHitTest(btnHard,x,y)){level=3;buttonPress(btnHard);}
            else if(buttonHitTest(btnStart,x,y)){buttonPress(btnStart);startGame();}
            else if(buttonHitTest(btnLeaderboard,x,y)){
                buttonPress(btnLeaderboard);
                enterLeaderboard();
            }
        }

        else if(currentScreen==SCREEN_PLAYING){

            if(buttonHitTest(btnHint,x,y) && !hintActive){
                buttonPress(btnHint);
                hintActive=1;

                for(int i=0;i<cardCount;i++){
                    cards[i].flipped=1;
                    cards[i].targetFlipAnim=1.0f;
                }

                glutTimerFunc(1000,hintTimer,0);
            }

            else if(buttonHitTest(btnRestart,x,y)){
                buttonPress(btnRestart);
                startGame();
            }

            else if(buttonHitTest(btnBack,x,y)){
                buttonPress(btnBack);
                goToStartScreen();
            }

            else if(buttonHitTest(btnLeaderboard,x,y)){
                buttonPress(btnLeaderboard);
                enterLeaderboard();
            }

            else{
                int idx = cardIndexFromMouse(x,y);
                if(idx!=-1) flipCard(idx);
            }
        }

        else if(currentScreen==SCREEN_WIN){
            if(buttonHitTest(btnPlayAgain,x,y)){
                buttonPress(btnPlayAgain);
                startGame();
            }
            else if(buttonHitTest(btnChange,x,y)){
                buttonPress(btnChange);
                goToStartScreen();
            }
            else if(buttonHitTest(btnLeaderboard,x,y)){
                buttonPress(btnLeaderboard);
                enterLeaderboard();
            }
        }

        else if(currentScreen==SCREEN_LEADERBOARD){
            if(buttonHitTest(btnLbBack,x,y)){
                buttonPress(btnLbBack);
                goToStartScreen();
            }
        }
    }
}

static void passiveMotion(int x,int y){
    mouseX=x; mouseY=y;
    if(currentScreen!=SCREEN_PLAYING){hoverCard=-1;return;}
    hoverCard=cardIndexFromMouse(x,y);
}

static void keyboard(unsigned char key,int x,int y){
    (void)x;(void)y;
    if(key==27)exit(0);
    if(key=='1')level=1; if(key=='2')level=2; if(key=='3')level=3;
    if(key==13){if(currentScreen==SCREEN_START||currentScreen==SCREEN_WIN)startGame();}
    else if(key=='r'||key=='R'){if(currentScreen==SCREEN_PLAYING)startGame();}
    else if(key=='b'||key=='B'){
        if(currentScreen==SCREEN_LEADERBOARD) goToStartScreen();
        else if(currentScreen==SCREEN_PLAYING) goToStartScreen();
    }
    else if(key=='l'||key=='L'){
        if(currentScreen==SCREEN_START || currentScreen==SCREEN_PLAYING || currentScreen==SCREEN_WIN)
            enterLeaderboard();
    }
    else if((key=='h'||key=='H')&&currentScreen==SCREEN_PLAYING&&!hintActive){
        hintActive=1;
        for(int i=0;i<cardCount;i++){cards[i].flipped=1;cards[i].targetFlipAnim=1.0f;}
        glutTimerFunc(1000,hintTimer,0);
    }
}

static void tick(int value){
    (void)value;
    globalTime+=0.016f;
    if(currentScreen==SCREEN_PLAYING)
        elapsedSeconds=(int)difftime(time(NULL),gameStartTime);

    for(int i=0;i<cardCount;i++){
        cards[i].flipAnim+=(cards[i].targetFlipAnim-cards[i].flipAnim)*0.18f;
        if(fabsf(cards[i].targetFlipAnim-cards[i].flipAnim)<0.01f)
            cards[i].flipAnim=cards[i].targetFlipAnim;

        cards[i].targetHoverAnim=(i==hoverCard)?1.0f:0.0f;
        cards[i].hoverAnim+=(cards[i].targetHoverAnim-cards[i].hoverAnim)*0.22f;

        if(cards[i].bounceAnim>0){cards[i].bounceAnim-=0.06f;if(cards[i].bounceAnim<0)cards[i].bounceAnim=0;}
        if(cards[i].glowAnim>0){cards[i].glowAnim-=0.01f;if(cards[i].glowAnim<0)cards[i].glowAnim=0;}

        /* frosted overlay fades in after match */
        if(cards[i].matched&&cards[i].frostedAlpha<1.0f)
            cards[i].frostedAlpha+=0.018f;

        if(cards[i].matched) cards[i].targetFlipAnim=1.0f;
        if(!cards[i].flipped&&!cards[i].matched) cards[i].targetFlipAnim=0.0f;
    }

    if(currentScreen==SCREEN_WIN){
        winAnim+=0.016f;
        if(winAnim>100.0f)winAnim=0.0f;
    }

    /* Leaderboard reveal animation */
    if(currentScreen==SCREEN_LEADERBOARD){
        if(lbRevealTime < 5.0f) lbRevealTime += 0.03f;
    }

    updateParticles();
    updateBanners();
    updateErrBanners();
    updateFlash();

    /* decay shake timers */
    for(int i=0;i<cardCount;i++){
        if(cardShakeTime[i]>0.0f){
            cardShakeTime[i]-=0.022f;   /* ~45 frames = ~0.72 s */
            if(cardShakeTime[i]<0.0f) cardShakeTime[i]=0.0f;
        }
    }

    glutPostRedisplay();
    glutTimerFunc(16,tick,0);
}

int main(int argc,char **argv){
    stbi_set_flip_vertically_on_load(1);
    srand((unsigned int)time(NULL));
    loadLeaderboard();

    /* ── Ask player name ── */
    printf("========================================\n");
    printf("   MEMORY MATCH - Cuisine Edition\n");
    printf("========================================\n");
    printf("\nEnter your name: ");
    scanf("%49s", currentPlayer);
    printf("\nWelcome, %s! Let's play!\n\n", currentPlayer);

    /* Check if returning player */
    for (int i = 0; i < playerCount; i++) {
        if (strcmp(leaderboard[i].name, currentPlayer) == 0) {
            printf("Welcome back, %s!\n", currentPlayer);
            printf("Your best: Time=%ds  Moves=%d  Accuracy=%.1f%%\n\n",
                leaderboard[i].time, leaderboard[i].moves, leaderboard[i].accuracy);
            break;
        }
    }

    initStars();
    memset(banners,   0, sizeof(banners));
    memset(errBanners,0, sizeof(errBanners));
    memset(cardShakeTime,0,sizeof(cardShakeTime));

    glutInit(&argc,argv);
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGBA);
    glutInitWindowSize(INIT_W,INIT_H);
    glutCreateWindow("Memory Match - Cuisine Edition");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.04f,0.05f,0.12f,1.0f);

    loadTextures();   /* NEW: load food images */

    applyLevel();
    computeLayout();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutMouseFunc(mouse);
    glutPassiveMotionFunc(passiveMotion);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(16,tick,0);
    glutMainLoop();
    return 0;
}