// main.cpp: initialisation & main loop

#include "cube.h"

void cleanup(char *msg)         // single program exit point;
{
	 stop();
    disconnect(true);
    cleangl();
    cleansound();
    cleanupserver();
    SDL_ShowCursor(1);
    if(msg)
    {
        #ifdef WIN32
        MessageBox(NULL, msg, "cube fatal error", MB_OK|MB_SYSTEMMODAL);
        #else
        printf(msg);
        #endif
    };
    SDL_Quit();
    exit(1);
};

void quit()                     // normal exit
{
    writeservercfg();
    cleanup(NULL);
};

void fatal(char *s, char *o)    // failure exit
{
    s_sprintfd(msg)("%s%s (%s)\n", s, o, SDL_GetError());
    cleanup(msg);
};

int scr_w = 640;
int scr_h = 480;

void screenshot()
{
    SDL_Surface *image;
    SDL_Surface *temp;
    int idx;
    if(image = SDL_CreateRGBSurface(SDL_SWSURFACE, scr_w, scr_h, 24, 0x0000FF, 0x00FF00, 0xFF0000, 0))
    {
        if(temp  = SDL_CreateRGBSurface(SDL_SWSURFACE, scr_w, scr_h, 24, 0x0000FF, 0x00FF00, 0xFF0000, 0))
        {
            glReadPixels(0, 0, scr_w, scr_h, GL_RGB, GL_UNSIGNED_BYTE, image->pixels);
            for (idx = 0; idx<scr_h; idx++)
            {
                char *dest = (char *)temp->pixels+3*scr_w*idx;
                memcpy(dest, (char *)image->pixels+3*scr_w*(scr_h-1-idx), 3*scr_w);
                endianswap(dest, 3, scr_w);
            };
            s_sprintfd(buf)("screenshots/screenshot_%d.bmp", lastmillis);
            SDL_SaveBMP(temp, path(buf));
            SDL_FreeSurface(temp);
        };
        SDL_FreeSurface(image);
    };
};

COMMAND(screenshot, ARG_NONE);
COMMAND(quit, ARG_NONE);

static void bar(float bar, int w, int o, float r, float g, float b)
{
    int side = 50;
    glColor3f(r, g, b);
    glVertex2f(side,                  o*FONTH);
    glVertex2f(bar*(w*3-2*side)+side, o*FONTH);
    glVertex2f(bar*(w*3-2*side)+side, (o+2)*FONTH);
    glVertex2f(side,                  (o+2)*FONTH);
};

void show_out_of_renderloop_progress(float bar1, const char *text1, float bar2, const char *text2)   // also used during loading
{
    int w = scr_w, h = scr_h;

    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w*3, h*3, 0, -1, 1);

    glBegin(GL_QUADS);

    if(text1)
    {
        bar(1,    w, 4, 0, 0,    0.8f);
        bar(bar1, w, 4, 0, 0.5f, 1);
    };

    if(bar2>0)
    {
        bar(1,    w, 6, 0.5f,  0, 0);
        bar(bar2, w, 6, 0.75f, 0, 0);
    };

    glEnd();

    glEnable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);

    if(text1) draw_text(text1, 70, 4*FONTH + FONTH/2);
    if(bar2>0) draw_text(text2, 70, 6*FONTH + FONTH/2);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glEnable(GL_DEPTH_TEST);
    SDL_GL_SwapBuffers();
};

SDL_Surface *screen = NULL;

extern void setfullscreen(bool enable);

VARF(fullscreen, 0, 0, 1, setfullscreen(fullscreen!=0));

void setfullscreen(bool enable)
{
    if(enable == !(screen->flags&SDL_FULLSCREEN))
    {
#ifdef WIN32
        conoutf("\"fullscreen\" variable not supported on this platform. Use the -t command-line option.");
        extern int fullscreen;
        fullscreen = !enable;
#else
        SDL_WM_ToggleFullScreen(screen);
        SDL_WM_GrabInput((screen->flags&SDL_FULLSCREEN) ? SDL_GRAB_ON : SDL_GRAB_OFF);
#endif
    };
};

void screenres(int *w, int *h, int *bpp = 0)
{
#ifdef WIN32
    conoutf("\"screenres\" command not supported on this platform. Use the -w and -h command-line options.");
#else
    SDL_Surface *surf = SDL_SetVideoMode(*w, *h, bpp ? *bpp : 0, SDL_OPENGL|SDL_RESIZABLE|(screen->flags&SDL_FULLSCREEN));
    if(!surf) return;
    scr_w = *w;
    scr_h = *h;
    screen = surf;
    glViewport(0, 0, *w, *h);
#endif
};

COMMAND(screenres, ARG_3INT);

VAR(maxfps, 5, 200, 500);

void limitfps(int &millis, int curmillis)
{
    static int fpserror = 0;
    int delay = 1000/maxfps - (millis-curmillis);
    if(delay < 0) fpserror = 0;
    else
    {
        fpserror += 1000%maxfps;
        if(fpserror >= maxfps)
        {
            ++delay;
            fpserror -= maxfps;
        };
        if(delay > 0)
        {
            SDL_Delay(delay);
            millis += delay;
        };
    };
};

int lowfps, highfps;

void fpsrange(int low, int high)
{
    if(low>high || low<1) return;
    lowfps = low;
    highfps = high;
};

COMMAND(fpsrange, ARG_2INT);

void keyrepeat(bool on)
{
    SDL_EnableKeyRepeat(on ? SDL_DEFAULT_REPEAT_DELAY : 0,
                             SDL_DEFAULT_REPEAT_INTERVAL);
};

VARF(gamespeed, 10, 100, 1000, if(multiplayer()) gamespeed = 100);

int islittleendian = 1;
int framesinmap = 0;

int main(int argc, char **argv)
{    
    int fs = SDL_FULLSCREEN, par = 0, uprate = 0, maxcl = 4;
    char *sdesc = "", *ip = "", *master = NULL, *passwd = "", *maprot = NULL;
    islittleendian = *((char *)&islittleendian);

    #define log(s) puts("init: " s)
    log("sdl");
    
    for(int i = 1; i<argc; i++)
    {
        char *a = &argv[i][2];
        if(argv[i][0]=='-') switch(argv[i][1])
        {
            case 't': fs     = 0; break;
            case 'w': scr_w  = atoi(a); break;
            case 'h': scr_h  = atoi(a); break;
            case 'u': uprate = atoi(a); break;
            case 'n': sdesc  = a; break;
            case 'i': ip     = a; break;
            case 'm': master = a; break;
            case 'p': passwd = a; break;
            case 'c': maxcl  = atoi(a); break; //EDIT: AH
            default:  conoutf("unknown commandline option");
        }
        else conoutf("unknown commandline argument");
    };
    
    #ifdef _DEBUG
    par = SDL_INIT_NOPARACHUTE;
    fs = 0;
    #endif

    if(SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO|par)<0) fatal("Unable to initialize SDL");

    log("net");
    if(enet_initialize()<0) fatal("Unable to initialise network module");

    initclient();
    initserver(false, uprate, sdesc, ip, master, passwd, maxcl, NULL, NULL);  // never returns if dedicated
      
    log("world");
    empty_world(7, true);

    log("video: sdl");
    if(SDL_InitSubSystem(SDL_INIT_VIDEO)<0) fatal("Unable to initialize SDL Video");

    log("video: mode");
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    #ifdef WIN32
        #ifdef SDL_RESIZABLE
        #undef SDL_RESIZABLE
        #endif
        #define SDL_RESIZABLE 0
    #endif
    screen = SDL_SetVideoMode(scr_w, scr_h, 0, SDL_OPENGL|SDL_RESIZABLE|fs);
    if(screen==NULL) fatal("Unable to create OpenGL screen");
    fullscreen = fs!=0;

    log("video: misc");
    SDL_WM_SetCaption("ActionCube", NULL);
    #ifndef WIN32
    if(fs)
    #endif
    SDL_WM_GrabInput(SDL_GRAB_ON);
    keyrepeat(false);
    SDL_ShowCursor(0);

    log("gl");
    gl_init(scr_w, scr_h);

    log("basetex");
    int xs, ys;
    if(!installtex(2,  path(newstring("packages/misc/newchars.png")), xs, ys) ||
       !installtex(3,  path(newstring("packages/misc/base.png")), xs, ys) ||
       !installtex(7,  path(newstring("packages/misc/smoke.png")), xs, ys) ||
	   !installtex(8,  path(newstring("packages/misc/full_logo.png")), xs, ys, false, true) ||
	   !installtex(6,  path(newstring("packages/textures/makke/menu.jpg")), xs, ys, false, true) ||
       !installtex(4,  path(newstring("packages/misc/explosion.jpg")), xs, ys) ||
       !installtex(5,  path(newstring("packages/misc/items.png")), xs, ys) ||
	   !installtex(9,  path(newstring("packages/misc/teammate.png")), xs, ys) ||
       !installtex(10, path(newstring("packages/misc/scope.png")), xs, ys) ||
       !installtex(11, path(newstring("packages/misc/flag_icons.png")), xs, ys) ||
       !installtex(1,  path(newstring("packages/misc/crosshairs/default.png")), xs, ys)) fatal("could not find core textures (hint: run cube from the parent of the bin directory)");

	loadingscreen();

    log("sound");
    initsound();

    log("cfg");
    newmenu("frags\tpj\tping\tteam\tname");
    newmenu("ping\tplr\tserver");
    newmenu("flags\tfrags\tpj\tping\tteam\tname");
	newmenu("kick player");
	newmenu("ban player");
    exec("config/keymap.cfg");
    exec("config/menus.cfg");
    exec("config/prefabs.cfg");
    exec("config/sounds.cfg");
    exec("config/servers.cfg");
    exec("config/autoexec.cfg");
    
    log("base models");
    loadweapons();

    log("localconnect");
    extern string clientmap;
    s_strcpy(clientmap, "maps/ac_complex");
    localconnect();
    
    log("mainloop");
    int ignore = 5, grabmouse = 0;
	int lastflush = 0;
    for(;;)
    {
        static int curmillis = 0, frames = 0;
        static float fps = 10.0f;
        int millis = SDL_GetTicks();
        limitfps(millis, curmillis);
        int elapsed = millis-curmillis;
        curtime = elapsed*gamespeed/100;
        if(curtime>200) curtime = 200;
        else if(curtime<1) curtime = 1;

        cleardlights();
        if(lastmillis) updateworld(curtime, lastmillis);

        lastmillis += curtime;
        curmillis = millis;

        if(!demoplayback) serverslice((int)time(NULL), 0);

        frames++;
        fps = (1000.0f/elapsed+fps*10)/11;

        computeraytable(player1->o.x, player1->o.y);
        readdepth(scr_w, scr_h);
        SDL_GL_SwapBuffers();
        extern void updatevol(); updatevol();
        if(framesinmap++<5)	// cheap hack to get rid of initial sparklies, even when triple buffering etc.
        {
			player1->yaw += 5;
			gl_drawframe(scr_w, scr_h, 1.0f, fps);
			player1->yaw -= 5;
        };
        gl_drawframe(scr_w, scr_h, fps<lowfps ? fps/lowfps : (fps>highfps ? fps/highfps : 1.0f), fps);
        //SDL_Delay(100);
        SDL_Event event;
        int lasttype = 0, lastbut = 0;
        while(SDL_PollEvent(&event))
        {
            switch(event.type)
            {
                case SDL_QUIT:
                    quit();
                    break;

                #ifndef WIN32
                case SDL_VIDEORESIZE:
                    screenres(&event.resize.w, &event.resize.h);
                    break;
                #endif

                case SDL_KEYDOWN: 
                case SDL_KEYUP: 
                    keypress(event.key.keysym.sym, event.key.state==SDL_PRESSED, event.key.keysym.unicode);
                    break;

                case SDL_ACTIVEEVENT:
                    if(event.active.state & SDL_APPINPUTFOCUS)
                        grabmouse = event.active.gain;
                    else
                    if(event.active.gain)
                        grabmouse = 1;
                    break;

                case SDL_MOUSEMOTION:
                    if(ignore) { ignore--; break; };
                    if(!(screen->flags&SDL_FULLSCREEN) && grabmouse)
                    {
                        #ifdef __APPLE__
                        if(event.motion.y == 0) break;  //let mac users drag windows via the title bar
                        #endif
                        if(event.motion.x == scr_w / 2 && event.motion.y == scr_h / 2) break;
                        SDL_WarpMouse(scr_w / 2, scr_h / 2);
                    };
                    #ifndef WIN32
                    if((screen->flags&SDL_FULLSCREEN) || grabmouse)
                    #endif
                    mousemove(event.motion.xrel, event.motion.yrel);
                    break;

                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    if(lasttype==event.type && lastbut==event.button.button) break; // why?? get event twice without it
                    keypress(-event.button.button, event.button.state!=0, 0);
                    lasttype = event.type;
                    lastbut = event.button.button;
                    break;
            };
        };
#ifdef _DEBUG
		if(millis>lastflush+60000) { 
			fflush(stdout); lastflush = millis; 
		}
#endif
    };
    quit();
    return 1;

};

void loadcrosshair(char *c)
{
	s_sprintfd(p)("packages/misc/crosshairs/%s", c);
	path(p);
	int xs, ys;
	installtex(1, p, xs, ys);

};

COMMAND(loadcrosshair, ARG_1STR);
