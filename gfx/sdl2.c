#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <stdio.h>
#include <unistd.h>

SDL_Window *myWindow = NULL;
SDL_Renderer *myRenderer = NULL;
SDL_Texture *myTexture = NULL;
int winw=0, winh=0, exposed=0, shown=0, mySdl2IsRunning=0;
int mousex=0, mousey=0, mouseb=0, mousez=0;
unsigned int currentColour = 0xffffffff; unsigned char currentColourR=255, currentColourG=255, currentColourB=255, currentColourA=255;

#include "font.c"

void handleEvents();

int startSdl2( char **errorMessageReturn, int w, int h){
 if( mySdl2IsRunning ){ 
  if( errorMessageReturn ) *errorMessageReturn = "startSdl2: SDL2 already started";
  return 1;
 }
 if( w < 1 ) w = 1;
 if( h < 1 ) h = 1;
 if( w > 4096 ) w = 4096;
 if( h > 4096 ) h = 4096;
 winw = w;  winh = h;
 // init sdl2
 if( SDL_Init( SDL_INIT_VIDEO ) ){
  if( errorMessageReturn ) *errorMessageReturn = (char*)SDL_GetError();
  return 1;
 }
 myWindow = SDL_CreateWindow( "SDL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE );
 // make window
 if( ! myWindow ){
  goto startSdl2_failure;
 }
 // make renderer
 myRenderer = SDL_CreateRenderer( myWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC );
 if( ! myRenderer ){
  SDL_DestroyWindow(myWindow);  goto startSdl2_failure;
 }
 // make texture
 myTexture = SDL_CreateTexture( myRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h );
 if( ! myTexture ){
  SDL_DestroyWindow(myWindow);  SDL_DestroyRenderer(myRenderer);  goto startSdl2_failure;
 }
 mySdl2IsRunning = 1;  initFont();
 while( ! shown ) handleEvents();
 return 0;
 startSdl2_failure:
 if( errorMessageReturn ) *errorMessageReturn = (char*)SDL_GetError();
 SDL_Quit();
 return 1;
}

void stopSdl2(){
 if( ! mySdl2IsRunning ) return;
 SDL_DestroyTexture(myTexture);
 SDL_DestroyRenderer(myRenderer);
 SDL_DestroyWindow(myWindow);
 SDL_Quit();
 myWindow = NULL;  myRenderer = NULL;  myTexture = NULL;
 mySdl2IsRunning = 0;
 destroyFont();
}

void updateDisplay(){
 if( ! mySdl2IsRunning ) return;
 SDL_SetRenderTarget( myRenderer, NULL);
 SDL_RenderCopy( myRenderer, myTexture, NULL, NULL );
 SDL_RenderPresent( myRenderer );
 SDL_SetRenderTarget( myRenderer, myTexture );
}

void drawLine( int x1, int y1, int x2, int y2 ){  if( ! mySdl2IsRunning ) return;
 lineColor( myRenderer, x1, y1, x2, y2, currentColour );
}

void drawPixel( int x, int y ){  if( ! mySdl2IsRunning ) return;
 //SDL_RenderDrawPoint( myRenderer, x, y);
 pixelColor( myRenderer, x, y, currentColour );
}

void drawRectangle( int x, int y, int w, int h ){  if( ! mySdl2IsRunning ) return;
 //SDL_Rect rect = (SDL_Rect){ x, y, w, h };  SDL_RenderDrawRect( myRenderer, &rect);
 rectangleColor( myRenderer, x, y, x+w, y+h, currentColour );
}

void fillRectangle( int x, int y, int w, int h ){  if( ! mySdl2IsRunning ) return;
 //SDL_Rect rect = (SDL_Rect){ x, y, w, h };  SDL_RenderFillRect( myRenderer, &rect);
 boxColor( myRenderer, x, y, x+w, y+h, currentColour );
}

void drawTriangle( int x1, int y1, int x2, int y2, int x3, int y3 ){  if( ! mySdl2IsRunning ) return;
 trigonColor( myRenderer, x1,y1,x2,y2,x3,y3, currentColour );
}

void fillTriangle( int x1, int y1, int x2, int y2, int x3, int y3 ){  if( ! mySdl2IsRunning ) return;
 filledTrigonColor( myRenderer, x1,y1,x2,y2,x3,y3, currentColour );
}

void drawCircle( int x, int y, int r ){  if( ! mySdl2IsRunning ) return;
 circleColor( myRenderer, x, y, r, currentColour );
}

void fillCircle( int x, int y, int r ){  if( ! mySdl2IsRunning ) return;
 filledCircleColor( myRenderer, x, y, r, currentColour );
}

void colour( unsigned char r, unsigned char g, unsigned char b, unsigned char a ){  if( ! mySdl2IsRunning ) return;
 SDL_SetRenderDrawColor( myRenderer, r, g, b, a );
 currentColourR = r;  currentColourR = g;  currentColourR = b;  currentColourR = a;  
 currentColour = a << 24 | b << 16 | g << 8 | r;
}

void clearScreen(){  if( ! mySdl2IsRunning ) return;
 boxColor( myRenderer, 0, 0, winw, winh, 0xff000000 );
}

void toggleVsync( int on ){  if( ! mySdl2IsRunning ) return;
 SDL_GL_SetSwapInterval( !!on );
}

void setBlendMode( unsigned int m ){  if( ! mySdl2IsRunning ) return;
 SDL_SetTextureBlendMode( myTexture, (int[]){ SDL_BLENDMODE_NONE, SDL_BLENDMODE_BLEND, SDL_BLENDMODE_ADD, SDL_BLENDMODE_MOD, SDL_BLENDMODE_MUL }[m % 5] );
}

void handleEvents(){  if( ! mySdl2IsRunning ) return;
 SDL_Event event;
 while( SDL_PollEvent(&event) ){
  switch( event.type ){
   case 256: exit(0);
   case SDL_MOUSEMOTION: 
    mousex = event.motion.x;  mousey = event.motion.y;
    break;
   case SDL_MOUSEBUTTONDOWN:
    mouseb |= 1 << (event.button.button-1);
    break;
   case SDL_MOUSEBUTTONUP:
    mouseb &= ~(1 << (event.button.button-1));
    break;
   case SDL_MOUSEWHEEL:
    mousez += event.wheel.y;
    break;
   case SDL_WINDOWEVENT: {
    switch( event.window.event ){
     case SDL_WINDOWEVENT_SHOWN:
      shown = 1;
      break;
     case SDL_WINDOWEVENT_EXPOSED:
      updateDisplay();
      // exposed = 1;
      break;
     case SDL_WINDOWEVENT_RESIZED:
      //printf("Window %d resized to %d x %d\n", event.window.windowID, event.window.data1, event.window.data2);
      SDL_DestroyTexture(myTexture);
      winw = event.window.data1;  winh = event.window.data2;  exposed = 1;
      myTexture = SDL_CreateTexture( myRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, winw, winh );
      SDL_SetRenderTarget( myRenderer, myTexture );  clearScreen();
      break;
     case SDL_WINDOWEVENT_CLOSE: exit(0);
     default:
      printf( "unhandled window event: type is %d\n", event.window.event );    
    }
   }
   break;
   default:
    printf( "unhandled: event.type is %d\n", event.type );
  }
 }
}

#ifdef MYTEST
double _v1=0, _v2=0;
double _rnd(double x){
 _v1 = ( _v1 + 0.1913892785834679832475832764736 );  _v1 = _v1 - (int) _v1;
 _v2 = _v2 * 4417.5818678214896982689218582765982 + _v1;
 _v2 = _v2 - (int) _v2;
 return _v2 * x;
}
int rnd(int x){
 return (int) _rnd( (double) x );
}

int main(){
 char *errorMessage;
 if( startSdl2( &errorMessage, 640, 480 ) ){
  printf( "failed to start SDL2: '%s'\n", errorMessage );
  return 1;
 }
 int this = 0;
 while( this < 16){
//  this += 1;
  //clearScreen();
#if 0 
  for(  int i=0;  i<2048;  i++  ){
   colour( rnd(100), rnd(150), rnd(255), 64 );
   //drawPixel( rnd(winw), rnd(winh) );
   fillRectangle( rnd(winw), rnd(winh), 4, 4 );
   //drawLine( rnd(winw), rnd(winh), rnd(winw), rnd(winh) );
  }
  colour( rnd(150), rnd(200), 255, 255 );
  drawCircle( rnd(winw), rnd(winh), rnd(winh)>>2 );
  colour( 255, 255, 255, 255 );
#endif
  //(mouseb ? fillCircle : drawCircle)( mousex, mousey, 7 );
  //drawScaledText( mousex, mousey, mousez, mousez, "Test message" );
  //colour( rnd(100), rnd(150), rnd(255), 255 );
  //drawTriangle( rnd(winw), rnd(winh), rnd(winw), rnd(winh), rnd(winw), rnd(winh) );
  for (int i=0;  i<6096;  i++) drawPixel( rnd(winw), rnd(winh) );
  updateDisplay();
  handleEvents();
  SDL_Delay(14);
  //printf("mousez %d\n", mousez);
 }
 stopSdl2();
}
#endif
