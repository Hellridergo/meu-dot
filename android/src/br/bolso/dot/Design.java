package br.bolso.dot;
import android.app.Activity;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.*;
import android.graphics.drawable.*;
import android.text.TextUtils;
import android.view.*;
import android.widget.*;

/** Lume design system: warm ink surfaces, the Dot's amber #F99D22 and Inter. */
final class Design {
  static final int BG=0xff0a0908, SURFACE=0xff15120f, SURFACE2=0xff1c1814, EDGE=0xff2a241e,
      INK=0xfffff5e8, MUTED=0xffa69a8c, FAINT=0xff6e645a,
      AMBER=0xfff99d22, AMBER_HI=0xffffb547, AMBER_LOW=0xffee8f14, AMBER_DEEP=0xffc9700c,
      ON_AMBER=0xff1f1205, AMBER_WASH=0x1ff99d22, MINT=0xff7fdca4, CORAL=0xffff7b5e;
  /** Former accent name, kept so older call sites read the new amber. */
  static final int SKY=AMBER;
  final Activity a;
  final Typeface regular, medium, semibold, display;
  Design(Activity a) {
    this.a=a;
    regular=face(430,16);medium=face(540,16);semibold=face(610,16);display=face(640,32);
    Window w=a.getWindow();
    w.setStatusBarColor(BG);w.setNavigationBarColor(BG);
    w.getDecorView().setSystemUiVisibility(0);w.setDecorFitsSystemWindows(false);
  }
  Typeface face(int weight,int optical) {
    try {
      Typeface t=new Typeface.Builder(a.getAssets(),"InterVariable.ttf")
          .setFontVariationSettings("'wght' "+weight+", 'opsz' "+optical).build();
      if(t!=null)return t;
    } catch(Exception ignored) {}
    return Typeface.create("sans-serif",weight>=600?Typeface.BOLD:Typeface.NORMAL);
  }
  int dp(float n) { return Math.round(n*a.getResources().getDisplayMetrics().density); }
  GradientDrawable shape(int fill,int radius,boolean border) {
    GradientDrawable d=new GradientDrawable();d.setColor(fill);d.setCornerRadius(dp(radius));
    if(border)d.setStroke(dp(1),EDGE);return d;
  }
  GradientDrawable gradient(int top,int bottom,int radius) {
    GradientDrawable d=new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,new int[]{top,bottom});
    d.setCornerRadius(dp(radius));return d;
  }
  static RippleDrawable ripple(Drawable content,Drawable mask) {
    return new RippleDrawable(ColorStateList.valueOf(0x29f99d22),content,mask);
  }
  LinearLayout column() { LinearLayout l=new LinearLayout(a);l.setOrientation(LinearLayout.VERTICAL);return l; }
  LinearLayout row() { LinearLayout l=new LinearLayout(a);l.setOrientation(LinearLayout.HORIZONTAL);l.setGravity(Gravity.CENTER_VERTICAL);return l; }
  TextView text(String s,float size,int color) {
    TextView t=new TextView(a);t.setText(s);t.setTextSize(size);t.setTextColor(color);
    t.setTypeface(size>=26?display:regular);t.setLineSpacing(dp(2),1f);
    if(size>=26)t.setLetterSpacing(-.03f);
    return t;
  }
  TextView text(String s,float size,int color,Typeface face) { TextView t=text(s,size,color);t.setTypeface(face);return t; }
  TextView line(LinearLayout p,String s,int size,int color,int top) {
    TextView t=text(s,size,color);add(p,t,top);return t;
  }
  void add(LinearLayout p,View v,int top) {
    LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.topMargin=dp(top);p.addView(v,lp);
  }
  TextView eyebrow(LinearLayout p,String s,int color,int top) {
    TextView t=text(s.toUpperCase(java.util.Locale.ROOT),11.5f,color,semibold);t.setLetterSpacing(.16f);add(p,t,top);return t;
  }
  void title(LinearLayout p,String over,String title,String body) {
    eyebrow(p,over,AMBER,10);
    TextView big=text(title,34,INK);big.setIncludeFontPadding(false);add(p,big,10);
    if(!body.isEmpty())add(p,text(body,15,MUTED),8);
  }
  Button button(LinearLayout p,String label,boolean primary,Runnable run) {
    Button b=new Button(a);b.setAllCaps(false);b.setText(label);b.setTextSize(15.5f);b.setTypeface(semibold);
    b.setLetterSpacing(0);b.setStateListAnimator(null);
    b.setTextColor(primary?ON_AMBER:INK);b.setMinHeight(dp(54));b.setMinimumHeight(dp(54));
    b.setPadding(dp(18),dp(12),dp(18),dp(12));
    Drawable face=primary?gradient(AMBER_HI,AMBER_LOW,18):shape(SURFACE2,18,true);
    b.setBackground(ripple(face,null));
    if(primary){b.setElevation(dp(8));b.setOutlineAmbientShadowColor(AMBER);b.setOutlineSpotShadowColor(AMBER);}
    b.setOnClickListener(v->run.run());add(p,b,12);return b;
  }
  EditText field(LinearLayout p,String title,String hint,boolean secret) {
    add(p,text(title,14,INK,medium),20);
    EditText e=new EditText(a);e.setTextSize(15.5f);e.setSingleLine();e.setTypeface(regular);
    e.setHint(hint);e.setTextColor(INK);e.setHintTextColor(FAINT);e.setHighlightColor(0x66f99d22);
    e.setInputType(secret?129:17);e.setSaveEnabled(false);e.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO);
    e.setPadding(dp(16),dp(15),dp(16),dp(15));e.setMinHeight(dp(56));
    GradientDrawable cursor=new GradientDrawable();cursor.setColor(AMBER);cursor.setSize(dp(2),dp(20));e.setTextCursorDrawable(cursor);
    StateListDrawable bg=new StateListDrawable();
    GradientDrawable focused=shape(SURFACE,16,false);focused.setStroke(dp(1.5f),AMBER);
    bg.addState(new int[]{android.R.attr.state_focused},focused);bg.addState(new int[]{},shape(SURFACE,16,true));
    e.setBackground(bg);add(p,e,8);return e;
  }
  LinearLayout card(LinearLayout p,int top) {
    LinearLayout c=column();c.setPadding(dp(20),dp(18),dp(20),dp(20));c.setBackground(shape(SURFACE,24,true));add(p,c,top);return c;
  }
  /** A card that holds list rows edge to edge. */
  LinearLayout group(LinearLayout p,int top) {
    LinearLayout c=column();c.setBackground(shape(SURFACE,24,true));c.setClipToOutline(true);add(p,c,top);return c;
  }
  View iconTile(int kind,int color) {
    FrameLayout f=new FrameLayout(a);GradientDrawable g=shape(SURFACE2,11,true);f.setBackground(g);
    f.addView(new NavIcon(a,kind,color),new FrameLayout.LayoutParams(dp(19),dp(19),Gravity.CENTER));
    f.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);return f;
  }
  private LinearLayout rowShell(LinearLayout group,int kind,int tint,String label,int labelColor) {
    if(group.getChildCount()>0){View rule=new View(a);rule.setBackgroundColor(EDGE);
      LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,Math.max(1,dp(.8f)));lp.leftMargin=dp(64);group.addView(rule,lp);}
    LinearLayout r=row();r.setPadding(dp(16),dp(12),dp(16),dp(12));r.setMinimumHeight(dp(60));
    r.addView(iconTile(kind,tint),new LinearLayout.LayoutParams(dp(34),dp(34)));
    TextView l=text(label,15.5f,labelColor,medium);l.setSingleLine();l.setEllipsize(TextUtils.TruncateAt.END);
    LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,-2,1);lp.leftMargin=dp(14);r.addView(l,lp);
    group.addView(r,new LinearLayout.LayoutParams(-1,-2));return r;
  }
  /** A list row: icon, label, optional value and chevron. Returns the value view. */
  TextView row(LinearLayout group,int kind,String label,String value,Runnable run) {
    return row(group,kind,AMBER,label,INK,value,run);
  }
  TextView row(LinearLayout group,int kind,int tint,String label,int labelColor,String value,Runnable run) {
    LinearLayout r=rowShell(group,kind,tint,label,labelColor);
    TextView v=text(value==null?"":value,13.5f,MUTED);v.setSingleLine();v.setGravity(Gravity.END);
    v.setMaxWidth(dp(170));v.setEllipsize(TextUtils.TruncateAt.END);r.addView(v,new LinearLayout.LayoutParams(-2,-2));
    if(run!=null){
      NavIcon chev=new NavIcon(a,NavIcon.CHEVRON,FAINT);LinearLayout.LayoutParams cp=new LinearLayout.LayoutParams(dp(18),dp(18));cp.leftMargin=dp(4);r.addView(chev,cp);
      r.setBackground(ripple(null,new ColorDrawable(0xffffffff)));r.setClickable(true);r.setFocusable(true);r.setOnClickListener(x->run.run());
      r.setContentDescription(label+(value==null||value.isEmpty()?"":", "+value));
    }
    return v;
  }
  Switch switchRow(LinearLayout group,int kind,String label,boolean checked,CompoundButton.OnCheckedChangeListener l) {
    LinearLayout r=rowShell(group,kind,AMBER,label,INK);
    Switch s=new Switch(a);s.setChecked(checked);s.setShowText(false);s.setContentDescription(label);
    GradientDrawable track=new GradientDrawable();track.setCornerRadius(dp(14));track.setSize(dp(46),dp(28));
    track.setColor(new ColorStateList(new int[][]{{android.R.attr.state_checked},{}},new int[]{AMBER,EDGE}));
    GradientDrawable knob=new GradientDrawable();knob.setShape(GradientDrawable.OVAL);knob.setSize(dp(22),dp(22));
    knob.setColor(new ColorStateList(new int[][]{{android.R.attr.state_checked},{}},new int[]{0xffffffff,MUTED}));
    s.setTrackDrawable(track);s.setThumbDrawable(new InsetDrawable(knob,dp(3)));s.setSwitchMinWidth(dp(46));
    s.setOnCheckedChangeListener(l);r.addView(s,new LinearLayout.LayoutParams(-2,-2));
    r.setOnClickListener(x->s.toggle());r.setBackground(ripple(null,new ColorDrawable(0xffffffff)));
    return s;
  }
  /** Status chip: a coloured dot and a short label over a translucent pill. */
  LinearLayout chip(String label,int dot) {
    LinearLayout c=row();c.setPadding(dp(11),dp(7),dp(12),dp(7));
    GradientDrawable g=shape(0x8c000000,999,true);c.setBackground(g);
    View d=new View(a);GradientDrawable o=new GradientDrawable();o.setShape(GradientDrawable.OVAL);o.setColor(dot);d.setBackground(o);
    c.addView(d,new LinearLayout.LayoutParams(dp(7),dp(7)));
    TextView t=text(label,12.5f,INK,medium);t.setSingleLine();LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-2,-2);lp.leftMargin=dp(7);c.addView(t,lp);
    return c;
  }
  static void chipState(LinearLayout chip,String label,int dot) {
    if(chip==null)return;
    ((GradientDrawable)chip.getChildAt(0).getBackground()).setColor(dot);
    update((TextView)chip.getChildAt(1),label);
  }
  /** Round back button with a label, at the top of inner pages. */
  void back(LinearLayout p,Runnable run) {
    LinearLayout r=row();r.setPadding(0,dp(4),0,dp(4));
    FrameLayout circle=new FrameLayout(a);circle.setBackground(ripple(shape(SURFACE2,999,true),null));
    circle.addView(new NavIcon(a,NavIcon.BACK,INK),new FrameLayout.LayoutParams(dp(22),dp(22),Gravity.CENTER));
    r.addView(circle,new LinearLayout.LayoutParams(dp(44),dp(44)));
    TextView t=text("Voltar",15,MUTED,medium);LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-2,-2);lp.leftMargin=dp(12);r.addView(t,lp);
    r.setClickable(true);r.setFocusable(true);r.setContentDescription("Voltar");r.setOnClickListener(v->run.run());
    LinearLayout.LayoutParams rp=new LinearLayout.LayoutParams(-2,-2);rp.topMargin=dp(4);p.addView(r,rp);
  }
  /** A spinner face: surface, hairline and a chevron at the end. */
  Drawable select() {
    LayerDrawable l=new LayerDrawable(new Drawable[]{shape(SURFACE,16,true),new NavIcon.Icon(NavIcon.CHEVRON,MUTED,dp(18))});
    l.setLayerGravity(1,Gravity.END|Gravity.CENTER_VERTICAL);l.setLayerInsetEnd(1,dp(16));
    l.setLayerSize(1,dp(18),dp(18));return l;
  }
  LinearLayout page() {
    ScrollView s=new ScrollView(a);s.setFillViewport(true);s.setBackgroundColor(BG);s.setClipToPadding(false);
    LinearLayout p=column();p.setPadding(dp(20),dp(12),dp(20),dp(36));s.addView(p);
    insets(s);a.setContentView(s);return p;
  }
  void insets(View v) {
    v.setOnApplyWindowInsetsListener((view,i)->{
      android.graphics.Insets b=i.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.ime());
      view.setPadding(b.left,b.top,b.right,b.bottom);return i;
    });
  }
  static void update(TextView t,String text) { if(t!=null&&!text.contentEquals(t.getText()))t.setText(text); }

  /** Slim level bar, used for the battery. A negative value hides the fill. */
  static final class Meter extends View {
    float value=-1;int color=MINT;final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    Meter(Context c){super(c);setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
    void set(float v,int c){if(v!=value||c!=color){value=v;color=c;invalidate();}}
    protected void onDraw(Canvas c){float h=getHeight(),w=getWidth();
      p.setColor(EDGE);c.drawRoundRect(0,0,w,h,h/2,h/2,p);
      if(value>=0){p.setColor(color);c.drawRoundRect(0,0,Math.max(h,w*Math.min(1,value)),h,h/2,h/2,p);}}
  }
}
