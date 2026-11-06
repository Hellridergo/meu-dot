package br.bolso.dot;
import android.content.Context;
import android.graphics.*;
import android.os.*;
import android.provider.Settings;
import android.view.View;

/**
 * The amber Dot from arte/personagem.png (four high-resolution poses). HERO floats
 * in its own halo, MINI is the Waveshare glass seen from above, AVATAR is a badge.
 */
final class DotView extends View {
  static final int HERO=0, MINI=1, AVATAR=2;
  static Bitmap sheet;
  final int style;
  final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
  final Handler h=new Handler(Looper.getMainLooper());
  boolean motion, grey;
  final Runnable tick=new Runnable(){public void run(){
    if(!isShown()||!isAttachedToWindow())return;
    invalidate();long t=SystemClock.uptimeMillis()%4150;
    h.postDelayed(this,t<360?16:4150-t);
  }};
  DotView(Context c){this(c,HERO);}
  DotView(Context c,int style){
    super(c);this.style=style;
    synchronized(DotView.class){
      if(sheet==null)try(java.io.InputStream in=c.getAssets().open("personagem.png")){sheet=BitmapFactory.decodeStream(in);}catch(Exception ignored){}
    }
    setContentDescription("Dot");setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
  }
  /** Draws the Dot without colour, as the badge does while the Dot is offline. */
  void setGrey(boolean g){if(g!=grey){grey=g;invalidate();}}
  protected void onAttachedToWindow(){super.onAttachedToWindow();
    motion=style!=AVATAR&&Settings.Global.getFloat(getContext().getContentResolver(),Settings.Global.ANIMATOR_DURATION_SCALE,1)>0;
    if(motion)h.post(tick);
  }
  protected void onDetachedFromWindow(){h.removeCallbacks(tick);super.onDetachedFromWindow();}
  static Rect pose(int i){int x=(i%2)*319,y=(i/2)*286;return new Rect(x+2,y+2,x+317,y+283);}
  void dot(Canvas c,float cx,float cy,float w){
    if(sheet==null)return;
    float hh=w*281/315;RectF to=new RectF(cx-w/2,cy-hh/2,cx+w/2,cy+hh/2);
    if(grey){ColorMatrix m=new ColorMatrix();m.setSaturation(.15f);ColorMatrix dim=new ColorMatrix();dim.setScale(.75f,.75f,.75f,1);m.postConcat(dim);p.setColorFilter(new ColorMatrixColorFilter(m));}
    p.setAlpha(255);c.drawBitmap(sheet,pose(0),to,p);
    long t=SystemClock.uptimeMillis()%4150;
    // A blink: the closed-eye pose fades in and out over a third of a second.
    if(motion&&t<330){float a=t<110?t/110f:t<190?1:(330-t)/140f;p.setAlpha(Math.round(255*a));c.drawBitmap(sheet,pose(2),to,p);p.setAlpha(255);}
    p.setColorFilter(null);
  }
  protected void onDraw(Canvas c){
    super.onDraw(c);float w=getWidth(),hgt=getHeight(),d=getResources().getDisplayMetrics().density;
    if(style==MINI){
      float cx=w/2,cy=hgt/2,r=Math.min(w,hgt)/2;
      p.setShader(new SweepGradient(cx,cy,new int[]{0xff2a2725,0xffa9a29c,0xff4d4844,0xff26221f,0xff9c948e,0xff3d3835,0xff2a2725},null));
      c.drawCircle(cx,cy,r,p);p.setShader(null);
      float inner=r-5*d;p.setColor(0xff000000);c.drawCircle(cx,cy,inner,p);
      if(!grey){p.setShader(new RadialGradient(cx,cy-inner*.05f,inner*.85f,new int[]{0x66f99d22,0x1ff99d22,0x00f99d22},new float[]{0,.55f,1},Shader.TileMode.CLAMP));
        c.drawCircle(cx,cy,inner,p);p.setShader(null);}
      p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(Math.max(1,d*.8f));p.setColor(0xff1d1915);c.drawCircle(cx,cy,inner*.94f,p);p.setStyle(Paint.Style.FILL);
      dot(c,cx,cy-inner*.04f,inner*1.08f);
    } else if(style==AVATAR){
      float cx=w/2,cy=hgt/2,r=Math.min(w,hgt)/2;
      p.setColor(Design.SURFACE2);c.drawCircle(cx,cy,r,p);
      c.save();Path clip=new Path();clip.addCircle(cx,cy,r-d,Path.Direction.CW);c.clipPath(clip);
      dot(c,cx,cy+r*.12f,r*2.05f);c.restore();
      p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(d);p.setColor(Design.EDGE);c.drawCircle(cx,cy,r-d/2,p);p.setStyle(Paint.Style.FILL);
    } else {
      float cx=w/2,cy=hgt*.47f,dw=Math.min(w*.48f,hgt*.58f*315/281);
      if(!grey){p.setShader(new RadialGradient(cx,cy,Math.min(w,hgt)*.62f,new int[]{0x55f99d22,0x1cf99d22,0x00f99d22},new float[]{0,.5f,1},Shader.TileMode.CLAMP));
        c.drawCircle(cx,cy,Math.min(w,hgt)*.62f,p);p.setShader(null);}
      // Contact shadow so the plush sits on the card instead of floating loose.
      float sy=cy+dw*281/315*.47f;
      p.setShader(new RadialGradient(cx,sy,dw*.42f,new int[]{0x66000000,0x00000000},null,Shader.TileMode.CLAMP));
      c.save();c.scale(1,.22f,cx,sy);c.drawCircle(cx,sy,dw*.42f,p);c.restore();p.setShader(null);
      dot(c,cx,cy,dw);
    }
  }
}
