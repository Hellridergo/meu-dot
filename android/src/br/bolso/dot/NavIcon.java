package br.bolso.dot;
import android.content.Context;
import android.graphics.*;
import android.graphics.drawable.Drawable;
import android.view.View;

/** Line icons drawn on a 24-unit grid with round caps, shared by views and drawables. */
final class NavIcon extends View {
  static final int HOME=0, CHAT=1, DEVICE=2, SETTINGS=3, MIC=4, WAVE=5, LINK=6, PLAY=7, CLOCK=8,
      POWER=9, CHIP=10, BLUETOOTH=11, BATTERY=12, CHEVRON=13, BACK=14, LOCK=15, OPEN=16, CHECK=17;
  final int kind;int color;final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
  NavIcon(Context c,int kind,int color){super(c);this.kind=kind;this.color=color;setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
  void tint(int c){if(c!=color){color=c;invalidate();}}
  protected void onDraw(Canvas canvas){super.onDraw(canvas);
    float s=Math.min(getWidth(),getHeight())/24f;
    draw(canvas,p,kind,color,(getWidth()-24*s)/2,(getHeight()-24*s)/2,s);
  }
  static void draw(Canvas canvas,Paint p,int kind,int color,float x,float y,float s){
    canvas.save();canvas.translate(x,y);canvas.scale(s,s);
    p.setColor(color);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1.7f);p.setStrokeJoin(Paint.Join.ROUND);p.setStrokeCap(Paint.Cap.ROUND);
    Path t=new Path();
    switch(kind){
      case HOME:t.moveTo(4,10.5f);t.lineTo(12,4);t.lineTo(20,10.5f);t.lineTo(20,20);t.lineTo(4,20);t.close();
        t.moveTo(10,20);t.lineTo(10,15);t.lineTo(14,15);t.lineTo(14,20);canvas.drawPath(t,p);break;
      case CHAT:t.moveTo(4,5.5f);t.lineTo(20,5.5f);t.lineTo(20,16.5f);t.lineTo(11,16.5f);t.lineTo(6.5f,20);t.lineTo(6.5f,16.5f);t.lineTo(4,16.5f);t.close();
        canvas.drawPath(t,p);canvas.drawLine(8,10,16,10,p);canvas.drawLine(8,13,13,13,p);break;
      case DEVICE:canvas.drawCircle(12,12,7.5f,p);canvas.drawLine(9,2.5f,15,2.5f,p);canvas.drawLine(9,21.5f,15,21.5f,p);
        p.setStyle(Paint.Style.FILL);canvas.drawCircle(12,12,2.2f,p);break;
      case SETTINGS:canvas.drawLine(4,7,6.2f,7,p);canvas.drawLine(11.8f,7,20,7,p);canvas.drawLine(4,17,12.2f,17,p);canvas.drawLine(17.8f,17,20,17,p);
        canvas.drawCircle(9,7,2.8f,p);canvas.drawCircle(15,17,2.8f,p);break;
      case MIC:canvas.drawRoundRect(9,3,15,14,3,3,p);canvas.drawArc(5.5f,4.5f,18.5f,17.5f,0,180,false,p);canvas.drawLine(12,17.5f,12,21,p);canvas.drawLine(9,21,15,21,p);break;
      case WAVE:canvas.drawLine(4,11,4,13,p);canvas.drawLine(8,8,8,16,p);canvas.drawLine(12,4.5f,12,19.5f,p);canvas.drawLine(16,8.5f,16,15.5f,p);canvas.drawLine(20,10.5f,20,13.5f,p);break;
      case LINK:t.moveTo(10,14);t.cubicTo(11.6f,15.6f,14.1f,15.6f,15.7f,14);t.lineTo(18.7f,11);t.cubicTo(20.3f,9.4f,20.3f,6.9f,18.7f,5.3f);t.cubicTo(17.1f,3.7f,14.6f,3.7f,13,5.3f);t.lineTo(12,6.3f);
        t.moveTo(14,10);t.cubicTo(12.4f,8.4f,9.9f,8.4f,8.3f,10);t.lineTo(5.3f,13);t.cubicTo(3.7f,14.6f,3.7f,17.1f,5.3f,18.7f);t.cubicTo(6.9f,20.3f,9.4f,20.3f,11,18.7f);t.lineTo(12,17.7f);canvas.drawPath(t,p);break;
      case PLAY:canvas.drawCircle(12,12,8.5f,p);t.moveTo(10.2f,8.8f);t.lineTo(10.2f,15.2f);t.lineTo(15.4f,12);t.close();p.setStyle(Paint.Style.FILL_AND_STROKE);canvas.drawPath(t,p);break;
      case CLOCK:canvas.drawCircle(12,12,8.5f,p);canvas.drawLine(12,7.5f,12,12,p);canvas.drawLine(12,12,15,14,p);break;
      case POWER:canvas.drawLine(12,3.5f,12,11.5f,p);canvas.drawArc(4.5f,5,19.5f,20,-50,280,false,p);break;
      case CHIP:canvas.drawRoundRect(6,6,18,18,3,3,p);for(int i=0;i<2;i++){float o=i==0?9.5f:14.5f;canvas.drawLine(o,3,o,6,p);canvas.drawLine(o,18,o,21,p);canvas.drawLine(3,o,6,o,p);canvas.drawLine(18,o,21,o,p);}
        p.setStyle(Paint.Style.FILL);canvas.drawRoundRect(9.5f,9.5f,14.5f,14.5f,1.5f,1.5f,p);break;
      case BLUETOOTH:t.moveTo(7,7.5f);t.lineTo(17,16.5f);t.lineTo(12,21);t.lineTo(12,3);t.lineTo(17,7.5f);t.lineTo(7,16.5f);canvas.drawPath(t,p);break;
      case BATTERY:canvas.drawRoundRect(3,7.5f,19,16.5f,2.5f,2.5f,p);canvas.drawLine(21.2f,10.5f,21.2f,13.5f,p);p.setStyle(Paint.Style.FILL);canvas.drawRoundRect(5.5f,10,13,14,1,1,p);break;
      case CHEVRON:t.moveTo(9.5f,6);t.lineTo(15.5f,12);t.lineTo(9.5f,18);canvas.drawPath(t,p);break;
      case BACK:t.moveTo(14.5f,6);t.lineTo(8.5f,12);t.lineTo(14.5f,18);canvas.drawPath(t,p);break;
      case LOCK:canvas.drawRoundRect(5,10.5f,19,20.5f,3,3,p);canvas.drawArc(8,4,16,14,180,180,false,p);canvas.drawLine(8,9,8,10.5f,p);canvas.drawLine(16,9,16,10.5f,p);break;
      case OPEN:canvas.drawRoundRect(4.5f,6.5f,17.5f,19.5f,3,3,p);canvas.drawLine(12,12,20,4,p);canvas.drawLine(14.5f,4,20,4,p);canvas.drawLine(20,4,20,9.5f,p);break;
      case CHECK:t.moveTo(5,12.5f);t.lineTo(10,17);t.lineTo(19,7);canvas.drawPath(t,p);break;
      default:break;
    }
    p.setStyle(Paint.Style.STROKE);canvas.restore();
  }
  /** The same icon as a drawable, for spinners and compound drawables. */
  static final class Icon extends Drawable {
    final int kind,color,size;final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    Icon(int kind,int color,int sizePx){this.kind=kind;this.color=color;this.size=sizePx;}
    public void draw(Canvas c){Rect b=getBounds();float s=Math.min(b.width(),b.height())/24f;
      NavIcon.draw(c,p,kind,color,b.left+(b.width()-24*s)/2,b.top+(b.height()-24*s)/2,s);}
    public int getIntrinsicWidth(){return size;}
    public int getIntrinsicHeight(){return size;}
    public void setAlpha(int a){p.setAlpha(a);}
    public void setColorFilter(ColorFilter f){p.setColorFilter(f);}
    public int getOpacity(){return PixelFormat.TRANSLUCENT;}
  }
}
