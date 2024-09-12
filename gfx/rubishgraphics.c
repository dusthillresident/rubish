#define RUBISH_NOT_STANDALONE
#include "../rubish.c"

#include "sdl2.c"

struct item primitive_StopGraphics( struct interp *interp, char **p ){
 stopSdl2();  return UNDEFINEDITEM;
}

struct item primitive_StartGraphics( struct interp *interp, char **p ){
 int w = 640, h = 480;
 if( paramsRemain(p) ){
  struct item width =  getNumber(interp,p);  if( width.type == ERROR  ) return width;
  struct item height = getNumber(interp,p);  if( height.type == ERROR ) return height;
  w = width.data.number;  h = height.data.number;
 }
 char *msg = NULL;
 if( startSdl2( &msg, w, h ) ){
  interp->errorMessage = msg;  return ERRORITEM(*p);
 }
 usleep(16.6*1000*2);
 return UNDEFINEDITEM;
}

struct item primitive_HandleEvents( struct interp *interp, char **p ){
 handleEvents();
 return UNDEFINEDITEM;
}

struct item primitive_UpdateDisplay( struct interp *interp, char **p ){
 updateDisplay();
 return UNDEFINEDITEM;
}

#define GFX_VAR_PRIMITIVE( NAME, VAR ) \
struct item primitive_##NAME( struct interp *interp, char **p ){ \
 struct item item;  item.type = NUMBER;  item.data.number = VAR ;  return item; \
}
GFX_VAR_PRIMITIVE( WinW, winw )
GFX_VAR_PRIMITIVE( WinH, winh )
GFX_VAR_PRIMITIVE( MouseX, mousex )
GFX_VAR_PRIMITIVE( MouseY, mousey )
GFX_VAR_PRIMITIVE( MouseZ, mousez )
GFX_VAR_PRIMITIVE( MouseB, mouseb )

struct item primitive_Colour( struct interp *interp, char **p ){
 unsigned char rgba[4]; rgba[3] = 255;
 struct item item;
 for(  int i=0;  i<3;  i++  ){
  if( i==1 && ! isValue( peekItem(p).type ) ){
   unsigned int c = item.data.number;  rgba[0] = c >> 16;  rgba[1] = c >> 8;  rgba[2] = c;  rgba[3] = (c >> 24) - 1;  goto primitive_Colour_setColour;
  }
  item = getNumber(interp,p);  if( item.type == ERROR ) return item;
  rgba[i] = item.data.number;
 }
 if( paramsRemain(p) ){
  item = getNumber(interp,p);  if( item.type == ERROR ) return item;
  rgba[3] = item.data.number;
 }
 primitive_Colour_setColour:
 colour( rgba[0], rgba[1], rgba[2], rgba[3] );
 return UNDEFINEDITEM;
}

struct item primitive_DrawPixel( struct interp *interp, char **p ){
 struct item x, y;
 x = getNumber(interp,p);  if( x.type == ERROR ) return x;
 y = getNumber(interp,p);  if( y.type == ERROR ) return y;
 drawPixel( x.data.number, y.data.number );
 return UNDEFINEDITEM;
}

struct item primitive_DrawLine( struct interp *interp, char **p ){
 struct item item;  int points[4]; 
 for(  int i=0;  i < 4;  i++  ){
  item = getNumber(interp,p);  if( item.type == ERROR ) return item;
  points[i] = item.data.number;
 }
 drawLine( points[0], points[1], points[2], points[3] );
 return UNDEFINEDITEM;
}

struct item primitive_Circle( struct interp *interp, char **p, int filled ){
 struct item item;  int points[3]; 
 for(  int i=0;  i < 3;  i++  ){
  item = getNumber(interp,p);  if( item.type == ERROR ) return item;
  points[i] = item.data.number;
 }
 (filled ? fillCircle : drawCircle)( points[0], points[1], points[2] );
 return UNDEFINEDITEM;
}
struct item primitive_DrawCircle( struct interp *interp, char **p ){
 return primitive_Circle( interp, p, 0 );
} 
struct item primitive_FillCircle( struct interp *interp, char **p ){
 return primitive_Circle( interp, p, 1 );
} 

struct item primitive_DrawText( struct interp *interp, char **p ){
 struct item item;  int points[4]; 
 for(  int i=0;  i < 4;  i++  ){
  item = getNumber(interp,p);  if( item.type == ERROR ) return item;
  points[i] = item.data.number;
 }
 struct item message = getNullTerminatedString(interp,p);  if( message.type == ERROR ) return message;
 drawScaledText( points[0], points[1], points[2], points[3], message.data.string.s );
 deleteItem( &message );
 return UNDEFINEDITEM;
}

struct item primitive_ToggleVsync( struct interp *interp, char **p ){
 struct item x = getNumber(interp,p);  if( x.type == ERROR ) return x;
 toggleVsync( x.data.number );
 x.data.number = !! x.data.number;
 return x;
}

int main(int argc, char **argv){

 struct interp *interp = calloc( 1, sizeof(struct interp) );
 // add these first, they are low priority
 installPrimitive( interp, primitive_StopGraphics, "stop-graphics" ); 
 installPrimitive( interp, primitive_StartGraphics, "start-graphics" );
 installPrimitive( interp, primitive_ToggleVsync, "toggle-vsync" );
 installPrimitive( interp, primitive_MouseX, "mousex" );
 installPrimitive( interp, primitive_MouseY, "mousey" );
 installPrimitive( interp, primitive_MouseZ, "mousez" );
 installPrimitive( interp, primitive_MouseB, "mouseb" );
 // now initialise the interpreter with the standard commands
 makeInterp( interp );
 installPrimitive( interp, primitive_HandleEvents, "handle-events" );
 installPrimitive( interp, primitive_UpdateDisplay, "update-display" );
 installPrimitive( interp, primitive_Colour, "colour" );
 installPrimitive( interp, primitive_DrawPixel, "draw-pixel" );
 installPrimitive( interp, primitive_DrawLine, "draw-line" );
 installPrimitive( interp, primitive_DrawCircle, "draw-circle" );
 installPrimitive( interp, primitive_FillCircle, "fill-circle" );
 installPrimitive( interp, primitive_DrawText, "draw-text" );
 installPrimitive( interp, primitive_WinW, "winw" );
 installPrimitive( interp, primitive_WinH, "winh" );
 
 struct item result = Rubish_main( interp, argc, argv ); 
 deleteItem( &result );
 deleteInterp( interp );
 if( result.type == NUMBER )
  return (int) result.data.number;
 else
  return 0;
 
 return 0;
}