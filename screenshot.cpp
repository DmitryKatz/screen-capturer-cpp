#define UNICODE
#define _UNICODE
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <shlobj.h>
#include <commdlg.h>

using namespace Gdiplus;
using std::min;
using std::max;
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

#define HOTKEY_ID    1
#define HOTKEY_MOD   MOD_CONTROL
#define HOTKEY_KEY   'Q'
#define WM_TRAYICON  (WM_USER+1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_OPEN 1002
#define WM_REGHOT    (WM_USER+2)
#define CURSOR_TIMER 42

enum class Tool { SelectRegion, Arrow, Rectangle, Circle, Blur, Text, Brush, Close };
enum class ArrowStyle { Normal, Outline, Double, Line };
struct BrushPoint { float x,y; };
enum class ObjType  { Arrow, Rectangle, Circle, Blur, Text, Brush };

struct Obj {
    ObjType  type     = ObjType::Arrow;
    Color    color    = Color(255,255,0,0);
    float    thick    = 3.f;
    bool     selected = false;
    float    x=0,y=0,w=200,h=60,angle=0;
    float    ax=0,ay=0,ax2=0,ay2=0;
    ArrowStyle arrowSt=ArrowStyle::Normal;
    int      blurR=15; float blurA=0.85f;
    std::wstring text=L"";
    std::wstring font=L"Arial";
    float    fsize=40.f;
    bool     bold=false,italic=false;
    std::vector<BrushPoint> pts;
};

struct TBtn { Tool tool; RECT rc; };
struct CBtn { Color col;  RECT rc; };

HBITMAP  hScr=NULL;
int      SW=0,SH=0;
std::vector<Obj>  objs;
int               selIdx=-1;
std::vector<TBtn> tbns;
std::vector<CBtn> cbns;
bool regSel=false;
RECT regRect={0,0,0,0};
Tool       curTool=Tool::SelectRegion;
Color      curCol(255,255,0,0);
float      curThick=3.f;
ArrowStyle curArrow=ArrowStyle::Normal;
int   blurR=15; float blurA=0.85f;
float fsize=40.f;
std::wstring fname=L"Arial";
bool  fbold=false,fitalic=false;
int   brushSz=8,selFont=0;

bool  drawing=false,dragging=false,resizing=false,rotating=false,brushing=false;
int   dragH=-1;
POINT mStart={0,0},mEnd={0,0};
float ox=0,oy=0,ow=0,oh=0,oangle=0,oax=0,oay=0,oax2=0,oay2=0;

// Текстовый ввод — всегда активен когда выделен текстовый объект
bool  editing=false;
int   editIdx=-1;
int   cursorPos=0;
bool  cursorVis=true;
UINT_PTR cursorTimer=0;

// Выделение текста мышью
bool  textSelecting=false;
int   textSelStart=-1,textSelEnd=-1; // -1 = нет выделения

HWND  hMain=NULL,hOver=NULL;
NOTIFYICONDATAW nid={};
HINSTANCE hInst=NULL;

const int TH=52,PH=40,BW=44,BH=38,CW=24,HR=7;
const int FPW=215;
RECT fpRect={0,0,0,0};

static inline INT I(double v){return (INT)v;}
static inline INT I(float  v){return (INT)v;}
static inline INT I(int    v){return v;}
static inline INT I(LONG   v){return (INT)v;}

int GetClsid(const WCHAR* f,CLSID* c){
    UINT n=0,s=0;GetImageEncodersSize(&n,&s);if(!s)return -1;
    auto* p=(ImageCodecInfo*)malloc(s);GetImageEncoders(n,s,p);
    for(UINT j=0;j<n;j++)if(!wcscmp(p[j].MimeType,f)){*c=p[j].Clsid;free(p);return(int)j;}
    free(p);return -1;
}
RECT NR(POINT a,POINT b){return{min(a.x,b.x),min(a.y,b.y),max(a.x,b.x),max(a.y,b.y)};}

void CopyToClip(HWND hw,HBITMAP hb){
    if(!OpenClipboard(hw))return;EmptyClipboard();
    BITMAP bm;GetObject(hb,sizeof(bm),&bm);int w=bm.bmWidth,h=bm.bmHeight;
    BITMAPINFOHEADER bi={sizeof(bi),w,-h,1,32,BI_RGB,0,0,0,0,0};
    HDC dc=GetDC(NULL),mc=CreateCompatibleDC(dc);SelectObject(mc,hb);
    HANDLE hd=GlobalAlloc(GMEM_MOVEABLE,sizeof(bi)+(size_t)w*h*4);
    if(hd){auto* p=(LPBYTE)GlobalLock(hd);memcpy(p,&bi,sizeof(bi));
        GetDIBits(mc,hb,0,h,p+sizeof(bi),(BITMAPINFO*)&bi,DIB_RGB_COLORS);
        GlobalUnlock(hd);SetClipboardData(CF_DIB,hd);}
    DeleteDC(mc);ReleaseDC(NULL,dc);CloseClipboard();
}

void BoxBlur(std::vector<DWORD>& px,int w,int h,int step){
    std::vector<DWORD> t((size_t)w*h);
    for(int y=0;y<h;y++)for(int x2=0;x2<w;x2++){
        int R=0,G=0,B=0,c=0;
        for(int k=-step;k<=step;k++){int nx=x2+k;if(nx<0||nx>=w)continue;
            DWORD v=px[y*w+nx];B+=v&0xFF;G+=(v>>8)&0xFF;R+=(v>>16)&0xFF;c++;}
        if(c)t[y*w+x2]=((R/c)<<16)|((G/c)<<8)|(B/c);}
    for(int y=0;y<h;y++)for(int x2=0;x2<w;x2++){
        int R=0,G=0,B=0,c=0;
        for(int k=-step;k<=step;k++){int ny=y+k;if(ny<0||ny>=h)continue;
            DWORD v=t[ny*w+x2];B+=v&0xFF;G+=(v>>8)&0xFF;R+=(v>>16)&0xFF;c++;}
        if(c)px[y*w+x2]=((R/c)<<16)|((G/c)<<8)|(B/c);}
}
void ApplyBlur(HDC dst,HBITMAP src,int rx,int ry,int rw,int rh,int rad,float op){
    if(rw<=0||rh<=0||rad<1)return;
    HDC sc=GetDC(NULL),sd=CreateCompatibleDC(sc);SelectObject(sd,src);
    HDC bd=CreateCompatibleDC(sc);HBITMAP bb=CreateCompatibleBitmap(sc,rw,rh);
    SelectObject(bd,bb);BitBlt(bd,0,0,rw,rh,sd,rx,ry,SRCCOPY);
    BITMAPINFO bmi={};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=rw;bmi.bmiHeader.biHeight=-rh;
    bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
    std::vector<DWORD> pixels((size_t)rw*rh);
    GetDIBits(bd,bb,0,rh,pixels.data(),&bmi,DIB_RGB_COLORS);
    int ps=max(1,rad/4+1),st=max(1,rad/ps);
    for(int p2=0;p2<ps;p2++)BoxBlur(pixels,rw,rh,st);
    SetDIBits(bd,bb,0,rh,pixels.data(),&bmi,DIB_RGB_COLORS);
    BLENDFUNCTION bf={AC_SRC_OVER,0,(BYTE)(op*255),0};
    AlphaBlend(dst,rx,ry,rw,rh,bd,0,0,rw,rh,bf);
    DeleteObject(bb);DeleteDC(bd);DeleteDC(sd);ReleaseDC(NULL,sc);
}

struct H{float x,y;};

// Для текстового объекта — маркеры только по периметру рамки (перетаскивание)
// Для остальных — стандартные 8+1
std::vector<H> GetHandles(const Obj& o){
    if(o.type==ObjType::Arrow)return{{o.ax,o.ay},{o.ax2,o.ay2}};
    if(o.type==ObjType::Text){
        // Только 4 угла — для перемещения рамки
        // Возвращаем пустой вектор — тексту не нужны маркеры масштабирования
        return{};
    }
    float cx=o.x+o.w/2,cy=o.y+o.h/2;
    float r=o.angle*(float)M_PI/180.f;
    auto rot=[&](float px,float py)->H{float dx=px-cx,dy=py-cy;
        return{cx+dx*cosf(r)-dy*sinf(r),cy+dx*sinf(r)+dy*cosf(r)};};
    std::vector<H> hs={
        rot(o.x,      o.y      ),
        rot(o.x+o.w/2,o.y      ),
        rot(o.x+o.w,  o.y      ),
        rot(o.x+o.w,  o.y+o.h/2),
        rot(o.x+o.w,  o.y+o.h  ),
        rot(o.x+o.w/2,o.y+o.h  ),
        rot(o.x,      o.y+o.h  ),
        rot(o.x,      o.y+o.h/2),
    };
    hs.push_back(rot(o.x+o.w/2,o.y-36));
    return hs;
}

int HitH(const Obj& o,POINT pt){
    auto hs=GetHandles(o);
    for(int i=0;i<(int)hs.size();i++){
        float dx=hs[i].x-pt.x,dy=hs[i].y-pt.y;
        if(dx*dx+dy*dy<=(HR+5)*(HR+5))return i;
    }return -1;
}

// Попадание в объект
bool HitObj(const Obj& o,POINT pt){
    if(o.type==ObjType::Arrow){
        float dx=o.ax2-o.ax,dy=o.ay2-o.ay,len=sqrtf(dx*dx+dy*dy);
        if(len<1)return false;
        float t=((pt.x-o.ax)*dx+(pt.y-o.ay)*dy)/(len*len);
        t=max(0.f,min(1.f,t));
        float px2=o.ax+t*dx,py2=o.ay+t*dy;
        float d=(pt.x-px2)*(pt.x-px2)+(pt.y-py2)*(pt.y-py2);
        float r2=max(8.f,o.thick+4);return d<=r2*r2;}
    if(o.type==ObjType::Brush){
        for(auto& bp:o.pts){float dx=bp.x-pt.x,dy=bp.y-pt.y;
            if(dx*dx+dy*dy<=(o.thick+8)*(o.thick+8))return true;}
        return false;}
    float cx=o.x+o.w/2,cy=o.y+o.h/2;
    float r=-o.angle*(float)M_PI/180.f;
    float dx=(float)(pt.x-cx),dy=(float)(pt.y-cy);
    float lx=dx*cosf(r)-dy*sinf(r),ly=dx*sinf(r)+dy*cosf(r);
    return lx>=-o.w/2&&lx<=o.w/2&&ly>=-o.h/2&&ly<=o.h/2;
}

// Попадание в рамку (периметр) текстового объекта — зона для перетаскивания
bool HitTextBorder(const Obj& o, POINT pt){
    if(o.type!=ObjType::Text) return false;
    const float bw=14.f; // ширина зоны рамки
    float x1=o.x,y1=o.y,x2=o.x+o.w,y2=o.y+o.h;
    // Внешний прямоугольник
    if(pt.x<x1-bw||pt.x>x2+bw||pt.y<y1-bw||pt.y>y2+bw) return false;
    // Внутренний — если внутри него, то НЕ рамка (там текст)
    if(pt.x>x1+bw&&pt.x<x2-bw&&pt.y>y1+bw&&pt.y<y2-bw) return false;
    return true;
}

Font* MakeFont(const Obj& o){
    int st=FontStyleRegular;
    if(o.bold)  st|=FontStyleBold;
    if(o.italic)st|=FontStyleItalic;
    FontFamily* ff=new FontFamily(o.font.c_str());
    if(ff->GetLastStatus()!=Ok){delete ff;ff=new FontFamily(L"Arial");}
    Font* f=new Font(ff,o.fsize,st,UnitPixel);
    delete ff;return f;
}

// Получить высоту одной строки
float GetLineHeight(Graphics& g, const Obj& o){
    Font* f=MakeFont(o);
    StringFormat sf;
    RectF lb;
    g.MeasureString(L"Ag",-1,f,PointF(0,0),&sf,&lb);
    delete f;
    return lb.Height;
}

// Получить позицию курсора в пикселях
void GetCursorPos2(Graphics& g, const Obj& o, int pos, float& outX, float& outY){
    Font* f=MakeFont(o);
    StringFormat sf;sf.SetAlignment(StringAlignmentNear);sf.SetLineAlignment(StringAlignmentNear);
    RectF lb;
    g.MeasureString(L"Ag",-1,f,PointF(0,0),&sf,&lb);
    float lineH=lb.Height;

    // Разбиваем текст до позиции на строки
    std::wstring sub=o.text.substr(0,min(pos,(int)o.text.size()));
    int lineIdx=0;
    std::wstring curLine=L"";
    for(wchar_t c:sub){
        if(c==L'\n'){lineIdx++;curLine=L"";}
        else curLine+=c;
    }
    // Ширина текущей строки
    float lineW=0;
    if(!curLine.empty()){
        RectF bound;
        g.MeasureString(curLine.c_str(),-1,f,PointF(0,0),&sf,&bound);
        lineW=bound.Width;
    }
    delete f;
    outX=o.x+lineW;
    outY=o.y+lineIdx*lineH;
}

// Получить позицию символа по пиксельным координатам (для клика мышью)
int GetCharPosFromPoint(Graphics& g, const Obj& o, POINT pt){
    Font* f=MakeFont(o);
    StringFormat sf;sf.SetAlignment(StringAlignmentNear);sf.SetLineAlignment(StringAlignmentNear);
    RectF lb;
    g.MeasureString(L"Ag",-1,f,PointF(0,0),&sf,&lb);
    float lineH=lb.Height;

    float relY=(float)(pt.y-o.y);
    int targetLine=(int)(relY/lineH);
    if(targetLine<0)targetLine=0;

    // Разбиваем текст на строки
    std::vector<std::wstring> lines;
    std::wstring cur=L"";
    std::vector<int> lineStarts; // индекс начала каждой строки в o.text
    int idx=0;
    lineStarts.push_back(0);
    for(int i=0;i<(int)o.text.size();i++){
        if(o.text[i]==L'\n'){lines.push_back(cur);cur=L"";lineStarts.push_back(i+1);}
        else cur+=o.text[i];
    }
    lines.push_back(cur);

    if(targetLine>=(int)lines.size())targetLine=(int)lines.size()-1;
    if(targetLine<0){delete f;return 0;}

    // Ищем позицию в строке по X
    float relX=(float)(pt.x-o.x);
    if(relX<0)relX=0;
    std::wstring& line=lines[targetLine];
    int bestPos=0;
    float bestDist=1e9f;
    for(int i=0;i<=(int)line.size();i++){
        float w=0;
        if(i>0){
            RectF bound;
            std::wstring sub2=line.substr(0,i);
            g.MeasureString(sub2.c_str(),-1,f,PointF(0,0),&sf,&bound);
            w=bound.Width;
        }
        float dist=fabsf(w-relX);
        if(dist<bestDist){bestDist=dist;bestPos=i;}
    }
    delete f;
    return lineStarts[targetLine]+bestPos;
}

// Пересчёт bbox
void UpdateTextBBox(Obj& o){
    if(!hOver)return;
    HDC dc=GetDC(hOver);
    Graphics g(dc);
    Font* f=MakeFont(o);
    StringFormat sf;sf.SetAlignment(StringAlignmentNear);sf.SetLineAlignment(StringAlignmentNear);
    sf.SetFormatFlags(StringFormatFlagsNoClip);
    RectF lb;
    g.MeasureString(L"Ag",-1,f,PointF(0,0),&sf,&lb);
    if(!o.text.empty()){
        RectF layout(0,0,4000,4000),bound;
        g.MeasureString(o.text.c_str(),-1,f,layout,&bound);
        o.w=max(bound.Width+30.f,80.f);
        o.h=max(bound.Height+16.f,lb.Height+16.f);
    } else {
        o.w=max(80.f,lb.Height*3);
        o.h=lb.Height+16.f;
    }
    delete f;ReleaseDC(hOver,dc);
}

void DrawArrow(Graphics& g,const Obj& o){
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    Pen pen(o.color,o.thick);
    double x1=o.ax,y1=o.ay,x2=o.ax2,y2=o.ay2;
    double dx=x2-x1,dy=y2-y1,len=sqrt(dx*dx+dy*dy);
    if(len<5)return;dx/=len;dy/=len;
    double aL=20+o.thick*2.5,aW=9+o.thick*2.0;
    if(o.arrowSt==ArrowStyle::Line){g.DrawLine(&pen,I(x1),I(y1),I(x2),I(y2));return;}
    if(o.arrowSt==ArrowStyle::Double){
        double bx=x1+dx*aL,by=y1+dy*aL,px=-dy,py=dx;
        PointF p1[3]={{(REAL)x1,(REAL)y1},{(REAL)(bx+px*aW),(REAL)(by+py*aW)},{(REAL)(bx-px*aW),(REAL)(by-py*aW)}};
        SolidBrush br(o.color);g.FillPolygon(&br,p1,3);
        double ex=x2-dx*aL,ey=y2-dy*aL;g.DrawLine(&pen,I(bx),I(by),I(ex),I(ey));
        PointF p2[3]={{(REAL)x2,(REAL)y2},{(REAL)(ex+px*aW),(REAL)(ey+py*aW)},{(REAL)(ex-px*aW),(REAL)(ey-py*aW)}};
        g.FillPolygon(&br,p2,3);return;}
    double bx=x2-dx*aL,by=y2-dy*aL,px=-dy,py=dx;
    g.DrawLine(&pen,I(x1),I(y1),I(bx),I(by));
    PointF pts[3]={{(REAL)x2,(REAL)y2},{(REAL)(bx+px*aW),(REAL)(by+py*aW)},{(REAL)(bx-px*aW),(REAL)(by-py*aW)}};
    SolidBrush br(o.color);g.FillPolygon(&br,pts,3);
    if(o.arrowSt==ArrowStyle::Outline){Pen op(Color(255,255,255,255),1.f);g.DrawPolygon(&op,pts,3);}
}

void DrawObj(Graphics& g,Obj& o,HDC hdc=NULL,bool isCursorVis=false){
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    Matrix saved;g.GetTransform(&saved);
    if(o.type!=ObjType::Arrow&&o.type!=ObjType::Brush&&o.type!=ObjType::Text&&o.angle!=0){
        float cx=o.x+o.w/2,cy=o.y+o.h/2;
        g.TranslateTransform(cx,cy);g.RotateTransform(o.angle);g.TranslateTransform(-cx,-cy);}
    Pen pen(o.color,o.thick);
    switch(o.type){
    case ObjType::Arrow:DrawArrow(g,o);break;
    case ObjType::Rectangle:g.DrawRectangle(&pen,I(o.x),I(o.y),I(o.w),I(o.h));break;
    case ObjType::Circle:g.DrawEllipse(&pen,I(o.x),I(o.y),I(o.w),I(o.h));break;
    case ObjType::Blur:
        if(hdc){HDC dc=g.GetHDC();
            ApplyBlur(dc,hScr,I(o.x),I(o.y),I(o.w),I(o.h),o.blurR,o.blurA);
            g.ReleaseHDC(dc);}break;
    case ObjType::Text:{
        Font* f=MakeFont(o);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentNear);
        sf.SetLineAlignment(StringAlignmentNear);
        sf.SetFormatFlags(StringFormatFlagsNoClip);

        // Выделение текста
        bool isEditing=(editing&&editIdx==(int)(&o-&objs[0]));
        if(isEditing&&textSelStart>=0&&textSelEnd>=0&&textSelStart!=textSelEnd){
            int selA=min(textSelStart,textSelEnd);
            int selB=max(textSelStart,textSelEnd);
            // Рисуем подсветку выделения (по строкам)
            float lh=GetLineHeight(g,o);
            // Упрощённо — одна полоса
            float sx1,sy1,sx2,sy2;
            GetCursorPos2(g,o,selA,sx1,sy1);
            GetCursorPos2(g,o,selB,sx2,sy2);
            if(I(sy1)==I(sy2)){
                SolidBrush selBr(Color(100,80,160,255));
                g.FillRectangle(&selBr,sx1,sy1,sx2-sx1,lh);
            } else {
                SolidBrush selBr(Color(100,80,160,255));
                // Первая строка
                g.FillRectangle(&selBr,sx1,sy1,o.x+o.w-sx1,lh);
                // Средние строки
                float curY=sy1+lh;
                while(curY+lh<sy2-1){
                    g.FillRectangle(&selBr,o.x,curY,o.w,lh);
                    curY+=lh;}
                // Последняя строка
                g.FillRectangle(&selBr,o.x,sy2,sx2-o.x,lh);
            }
        }

        // Текст
        SolidBrush br(o.color);
        RectF layout(o.x,o.y,4000.f,4000.f);
        if(!o.text.empty()){
            g.DrawString(o.text.c_str(),-1,f,layout,&sf,&br);
        } else if(isEditing){
            // Пустое поле в режиме редактирования — ничего не рисуем
        } else {
            SolidBrush ph(Color(100,180,180,180));
            g.DrawString(L"Текст...",-1,f,layout,&sf,&ph);
        }

        // Мигающий курсор
        if(isEditing&&isCursorVis){
            float cx2,cy2;
            GetCursorPos2(g,o,cursorPos,cx2,cy2);
            float lh=GetLineHeight(g,o);
            Pen cp2(o.color,2.f);
            g.DrawLine(&cp2,cx2,cy2,cx2,cy2+lh);
        }
        delete f;

        // Рамка вокруг текстового объекта (всегда видна когда выделен)
        if(o.selected){
            Pen framePen(Color(180,80,160,255),1.5f);
            REAL dd[]={5,3};framePen.SetDashPattern(dd,2);
            g.DrawRectangle(&framePen,I(o.x),I(o.y),I(o.w),I(o.h));
     // иконка перемещения (4 стрелки) в центре рамки
            float cx3=o.x+o.w/2,cy3=o.y+o.h/2;
            Pen movePen(Color(200,255,255,255),1.5f);
            SolidBrush moveArrow(Color(200,255,255,255));
            int ms=8;
            g.DrawLine(&movePen,cx3-ms,cy3,cx3+ms,cy3);
            g.DrawLine(&movePen,cx3,cy3-ms,cx3,cy3+ms);
            PointF apts[3];
            // вверх
            apts[0]={cx3,cy3-ms};apts[1]={cx3-4,cy3-ms+5};apts[2]={cx3+4,cy3-ms+5};
            g.FillPolygon(&moveArrow,apts,3);
            // вниз
            apts[0]={cx3,cy3+ms};apts[1]={cx3-4,cy3+ms-5};apts[2]={cx3+4,cy3+ms-5};
            g.FillPolygon(&moveArrow,apts,3);
            // влево
            apts[0]={cx3-ms,cy3};apts[1]={cx3-ms+5,cy3-4};apts[2]={cx3-ms+5,cy3+4};
            g.FillPolygon(&moveArrow,apts,3);
            // вправо
            apts[0]={cx3+ms,cy3};apts[1]={cx3+ms-5,cy3-4};apts[2]={cx3+ms-5,cy3+4};
            g.FillPolygon(&moveArrow,apts,3);

            // Кнопка удаления
            float delX=o.x+o.w+4,delY=o.y-28;
            SolidBrush db(Color(240,210,50,50));
            g.FillEllipse(&db,I(delX),I(delY),24,24);
            Pen dp(Color(255,255,255,255),2.2f);
            g.DrawLine(&dp,I(delX+6),I(delY+6),I(delX+18),I(delY+18));
            g.DrawLine(&dp,I(delX+18),I(delY+6),I(delX+6),I(delY+18));
        }
        break;}
    case ObjType::Brush:
        if(o.pts.size()<2)break;
        {Pen bp(o.color,o.thick);bp.SetStartCap(LineCapRound);bp.SetEndCap(LineCapRound);bp.SetLineJoin(LineJoinRound);
        for(size_t i=1;i<o.pts.size();i++)
            g.DrawLine(&bp,I(o.pts[i-1].x),I(o.pts[i-1].y),I(o.pts[i].x),I(o.pts[i].y));break;}
    }
    g.SetTransform(&saved);

    // Для не-текстовых объектов — стандартные маркеры
    if(o.type==ObjType::Text||!o.selected)return;

    auto hs=GetHandles(o);
    Pen sp(Color(255,80,160,255),1.5f);REAL d[]={5,3};sp.SetDashPattern(d,2);
    if(o.type==ObjType::Arrow){
        g.DrawLine(&sp,I(hs[0].x),I(hs[0].y),I(hs[1].x),I(hs[1].y));
    } else {
        float cx2=o.x+o.w/2,cy2=o.y+o.h/2;
        float r2=o.angle*(float)M_PI/180.f;
        auto rot=[&](float px,float py)->PointF{float dx=px-cx2,dy=py-cy2;
            return{cx2+dx*cosf(r2)-dy*sinf(r2),cy2+dx*sinf(r2)+dy*cosf(r2)};};
        PointF corners[4]={rot(o.x,o.y),rot(o.x+o.w,o.y),rot(o.x+o.w,o.y+o.h),rot(o.x,o.y+o.h)};
        g.DrawPolygon(&sp,corners,4);
        auto tm=rot(o.x+o.w/2,o.y);
        Pen rl(Color(150,80,160,255),1.f);g.DrawLine(&rl,tm.X,tm.Y,hs[8].x,hs[8].y);
    }
    for(int i=0;i<(int)hs.size();i++){
        bool isRot=(i==8);
        if(isRot){
            SolidBrush hbr(Color(255,60,200,80));
            Pen hp(Color(255,255,255,255),1.5f);
            g.FillEllipse(&hbr,I(hs[i].x-HR),I(hs[i].y-HR),HR*2,HR*2);
            g.DrawEllipse(&hp,I(hs[i].x-HR),I(hs[i].y-HR),HR*2,HR*2);
        } else {
            SolidBrush hbr(Color(255,255,255,255));
            Pen hp(Color(255,50,120,220),2.f);
            g.FillRectangle(&hbr,I(hs[i].x-HR),I(hs[i].y-HR),HR*2,HR*2);
            g.DrawRectangle(&hp,I(hs[i].x-HR),I(hs[i].y-HR),HR*2,HR*2);
        }
    }
    // Кнопка удаления для не-текстовых
    float delX,delY;
    if(o.type==ObjType::Arrow){delX=(hs[0].x+hs[1].x)/2-12;delY=min(hs[0].y,hs[1].y)-32;}
    else{delX=hs[2].x+6;delY=hs[2].y-28;}
    SolidBrush db(Color(240,210,50,50));
    g.FillEllipse(&db,I(delX),I(delY),24,24);
    Pen dp(Color(255,255,255,255),2.2f);
    g.DrawLine(&dp,I(delX+6),I(delY+6),I(delX+18),I(delY+18));
    g.DrawLine(&dp,I(delX+18),I(delY+6),I(delX+6),I(delY+18));
}

bool HitDelText(const Obj& o, POINT pt){
    if(o.type!=ObjType::Text)return false;
    float delX=o.x+o.w+4,delY=o.y-28;
    return pt.x>=delX&&pt.x<=delX+24&&pt.y>=delY&&pt.y<=delY+24;
}

bool HitDel(const Obj& o,POINT pt){
    if(o.type==ObjType::Text)return HitDelText(o,pt);
    auto hs=GetHandles(o);
    float delX,delY;
    if(o.type==ObjType::Arrow){delX=(hs[0].x+hs[1].x)/2-12;delY=min(hs[0].y,hs[1].y)-32;}
    else{delX=hs[2].x+6;delY=hs[2].y-28;}
    return pt.x>=delX&&pt.x<=delX+24&&pt.y>=delY&&pt.y<=delY+24;
}

void DoResize(Obj& o,int h2,POINT pt){
    if(o.type==ObjType::Arrow){
        if(h2==0){o.ax=(float)pt.x;o.ay=(float)pt.y;}
        else{o.ax2=(float)pt.x;o.ay2=(float)pt.y;}return;}
    float cx2=ox+ow/2,cy2=oy+oh/2;
    float r=-o.angle*(float)M_PI/180.f;
    float dx=(float)(pt.x-cx2),dy=(float)(pt.y-cy2);
    float lx=dx*cosf(r)-dy*sinf(r)+ow/2,ly=dx*sinf(r)+dy*cosf(r)+oh/2;
    switch(h2){
    case 0:o.x=ox+lx;o.y=oy+ly;o.w=ow-lx;o.h=oh-ly;break;
    case 1:o.y=oy+ly;o.h=oh-ly;break;
    case 2:o.w=lx;o.y=oy+ly;o.h=oh-ly;break;
    case 3:o.w=lx;break;
    case 4:o.w=lx;o.h=ly;break;
    case 5:o.h=ly;break;
    case 6:o.x=ox+lx;o.w=ow-lx;o.h=ly;break;
    case 7:o.x=ox+lx;o.w=ow-lx;break;}
    if(o.w<20)o.w=20;if(o.h<20)o.h=20;
}

// Запуск редактирования текста
void StartTextEdit(int idx){
    if(idx<0||idx>=(int)objs.size())return;
    editing=true;editIdx=idx;
    cursorPos=(int)objs[idx].text.size();
    cursorVis=true;textSelStart=-1;textSelEnd=-1;
    if(cursorTimer)KillTimer(hOver,CURSOR_TIMER);
    cursorTimer=SetTimer(hOver,CURSOR_TIMER,500,NULL);
    fname  =objs[idx].font;
    fsize  =objs[idx].fsize;
    fbold  =objs[idx].bold;
    fitalic=objs[idx].italic;
    curCol =objs[idx].color;
    SetFocus(hOver);SetForegroundWindow(hOver);
}

void StopTextEdit(){
    if(!editing)return;
    if(cursorTimer){KillTimer(hOver,CURSOR_TIMER);cursorTimer=0;}
    if(editIdx>=0&&editIdx<(int)objs.size()){
        if(objs[editIdx].text.empty()){
            objs.erase(objs.begin()+editIdx);
            if(selIdx>=editIdx)selIdx=-1;
        } else {
            UpdateTextBBox(objs[editIdx]);
        }
    }
    editing=false;editIdx=-1;cursorPos=0;
    textSelStart=-1;textSelEnd=-1;textSelecting=false;
    InvalidateRect(hOver,NULL,FALSE);
}

void TextInsert(wchar_t ch){
    if(editIdx<0||editIdx>=(int)objs.size())return;
    auto& o=objs[editIdx];
    // Удалить выделение если есть
    if(textSelStart>=0&&textSelEnd>=0&&textSelStart!=textSelEnd){
        int a=min(textSelStart,textSelEnd),b=max(textSelStart,textSelEnd);
        o.text.erase(a,b-a);cursorPos=a;
        textSelStart=textSelEnd=-1;}
    if(cursorPos>(int)o.text.size())cursorPos=(int)o.text.size();
    o.text.insert(o.text.begin()+cursorPos,ch);
    cursorPos++;
    UpdateTextBBox(o);
    cursorVis=true;InvalidateRect(hOver,NULL,FALSE);
}
void TextBackspace(){
    if(editIdx<0||editIdx>=(int)objs.size())return;
    auto& o=objs[editIdx];
    if(textSelStart>=0&&textSelEnd>=0&&textSelStart!=textSelEnd){
        int a=min(textSelStart,textSelEnd),b=max(textSelStart,textSelEnd);
        o.text.erase(a,b-a);cursorPos=a;
        textSelStart=textSelEnd=-1;
        UpdateTextBBox(o);cursorVis=true;InvalidateRect(hOver,NULL,FALSE);return;}
    if(cursorPos>0&&cursorPos<=(int)o.text.size()){
        o.text.erase(o.text.begin()+cursorPos-1);cursorPos--;}
    UpdateTextBBox(o);cursorVis=true;InvalidateRect(hOver,NULL,FALSE);
}
void TextDelete(){
    if(editIdx<0||editIdx>=(int)objs.size())return;
    auto& o=objs[editIdx];
    if(textSelStart>=0&&textSelEnd>=0&&textSelStart!=textSelEnd){
        int a=min(textSelStart,textSelEnd),b=max(textSelStart,textSelEnd);
        o.text.erase(a,b-a);cursorPos=a;
        textSelStart=textSelEnd=-1;
        UpdateTextBBox(o);cursorVis=true;InvalidateRect(hOver,NULL,FALSE);return;}
    if(cursorPos<(int)o.text.size())
        o.text.erase(o.text.begin()+cursorPos);
    UpdateTextBBox(o);cursorVis=true;InvalidateRect(hOver,NULL,FALSE);
}

struct FP{std::wstring fn,label;bool bold,italic;};
std::vector<FP> fps={
    {L"Arial",         L"Классика",   false,false},
    {L"Segoe Script",  L"Прописью",   false,false},
    {L"Impact",        L"Фломастер",  false,false},
    {L"Arial",         L"Курсив",     false,true },
    {L"Courier New",   L"Печатный",   false,false},
    {L"Arial Black",   L"Плакат",     true, false},
    {L"Georgia",       L"Ретро",      false,true },
    {L"Arial",         L"Строгий",    true, false},
    {L"Comic Sans MS", L"Лобстер",    false,false},
};
Color fpCols[]={
    Color(255,255,255,255),Color(255,0,0,0),
    Color(255,255,0,0),    Color(255,0,200,0),
    Color(255,30,144,255), Color(255,255,200,0),
    Color(255,255,165,0),  Color(255,200,0,200),
    Color(255,0,200,200),  Color(255,180,180,180),
    Color(255,255,120,120),Color(255,120,255,120),
};
bool IsFontMode(){
    return curTool==Tool::Text||
           (selIdx>=0&&selIdx<(int)objs.size()&&objs[selIdx].type==ObjType::Text);
}
void ApplyFontToSel(){
    if(selIdx>=0&&selIdx<(int)objs.size()&&objs[selIdx].type==ObjType::Text){
        objs[selIdx].font  =fname;objs[selIdx].bold=fbold;
        objs[selIdx].italic=fitalic;objs[selIdx].fsize=fsize;objs[selIdx].color=curCol;
        UpdateTextBBox(objs[selIdx]);
        // Обновляем курсор редактирования
        if(editing&&editIdx==selIdx)curCol=objs[selIdx].color;
    }
}

void DrawFontPanel(HDC hdc,int W,int H){
    if(!IsFontMode())return;
    int pw=FPW,ph=min(H-TH-PH-20,600);
    int px=W-pw-6,py=TH+12;
    fpRect={px,py,px+pw,py+ph};
    HBRUSH bg=CreateSolidBrush(RGB(24,24,24));FillRect(hdc,&fpRect,bg);DeleteObject(bg);
    Graphics g(hdc);g.SetSmoothingMode(SmoothingModeAntiAlias);
    Pen bp2(Color(255,60,60,60),1.f);g.DrawRectangle(&bp2,I(px),I(py),I(pw),I(ph));
    SetBkMode(hdc,TRANSPARENT);
    HFONT hft=CreateFontW(12,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    HFONT hfo=(HFONT)SelectObject(hdc,hft);
    SetTextColor(hdc,RGB(160,160,160));TextOutW(hdc,px+10,py+8,L"Шрифт",5);
    SelectObject(hdc,hfo);DeleteObject(hft);
    int yy=py+26,ih=30;
    for(int i=0;i<(int)fps.size();i++){
        bool act=(i==selFont);
        RECT ir={px+3,yy,px+pw-3,yy+ih-1};
        if(act){HBRUSH ab=CreateSolidBrush(RGB(40,85,155));FillRect(hdc,&ir,ab);DeleteObject(ab);}
        int fs2=act?14:12;
        HFONT hf=CreateFontW(-fs2,0,0,0,fps[i].bold?FW_BOLD:FW_NORMAL,(BYTE)fps[i].italic,0,0,
            DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,fps[i].fn.c_str());
        HFONT hfo2=(HFONT)SelectObject(hdc,hf);
        SetTextColor(hdc,act?RGB(255,255,255):RGB(165,165,165));
        RECT tr=ir;tr.left+=10;tr.right-=4;
        DrawTextW(hdc,fps[i].label.c_str(),-1,&tr,DT_VCENTER|DT_SINGLELINE|DT_LEFT);
        SelectObject(hdc,hfo2);DeleteObject(hf);yy+=ih;
    }
    yy+=4;
    HPEN lp2=CreatePen(PS_SOLID,1,RGB(50,50,50));HPEN lop=(HPEN)SelectObject(hdc,lp2);
    MoveToEx(hdc,px+8,yy,NULL);LineTo(hdc,px+pw-8,yy);SelectObject(hdc,lop);DeleteObject(lp2);yy+=8;
    HFONT hfl=CreateFontW(11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    HFONT hflo=(HFONT)SelectObject(hdc,hfl);
    SetTextColor(hdc,RGB(120,120,120));
    wchar_t sb[32];swprintf_s(sb,L"Размер: %.0f",fsize);
    TextOutW(hdc,px+10,yy,sb,(int)wcslen(sb));yy+=16;
    int slx=px+8,slw=pw-16;
    RECT slr2={slx,yy+5,slx+slw,yy+11};
    HBRUSH slbg=CreateSolidBrush(RGB(45,45,45));FillRect(hdc,&slr2,slbg);DeleteObject(slbg);
    float t2=(fsize-8.f)/(150.f-8.f);int spos=(int)(t2*(slw-12));
    RECT slFill={slx,yy+5,slx+spos,yy+11};
    HBRUSH slFb=CreateSolidBrush(RGB(60,120,220));FillRect(hdc,&slFill,slFb);DeleteObject(slFb);
    RECT slHnd={slx+spos-4,yy+2,slx+spos+8,yy+14};
    HBRUSH slHb=CreateSolidBrush(RGB(255,255,255));FillRect(hdc,&slHnd,slHb);DeleteObject(slHb);
    yy+=20;yy+=8;
    auto mkBut=[&](LPCWSTR txt,int bx,int by2,int bw,int bh,bool act)->void{
        RECT br={bx,by2,bx+bw,by2+bh};
        HBRUSH bb2=CreateSolidBrush(act?RGB(50,105,200):RGB(38,38,38));
        FillRect(hdc,&br,bb2);DeleteObject(bb2);
        HPEN hp2=CreatePen(PS_SOLID,1,act?RGB(80,140,230):RGB(58,58,58));
        HPEN ho=(HPEN)SelectObject(hdc,hp2);
        HBRUSH nb=(HBRUSH)GetStockObject(NULL_BRUSH);HBRUSH ob=(HBRUSH)SelectObject(hdc,nb);
        Rectangle(hdc,br.left,br.top,br.right,br.bottom);
        SelectObject(hdc,ob);SelectObject(hdc,ho);DeleteObject(hp2);
        HFONT hfb=CreateFontW(-13,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        HFONT hfbo=(HFONT)SelectObject(hdc,hfb);
        SetTextColor(hdc,act?RGB(255,255,255):RGB(130,130,130));
        DrawTextW(hdc,txt,-1,&br,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc,hfbo);DeleteObject(hfb);
    };
    mkBut(L"B",px+8,yy,46,24,fbold);
    mkBut(L"I",px+60,yy,46,24,fitalic);
    yy+=32;yy+=6;
    int cs=26,cg=3,cc=6;
    for(int i=0;i<12;i++){
        int cx3=px+8+(i%cc)*(cs+cg);int cy3=yy+(i/cc)*(cs+cg);
        bool act=(fpCols[i].GetValue()==curCol.GetValue());
        SolidBrush cb(fpCols[i]);
        g.FillEllipse(&cb,I(cx3+1),I(cy3+1),cs-2,cs-2);
        if(act){Pen cp(Color(255,255,255,255),2.2f);g.DrawEllipse(&cp,I(cx3),I(cy3),cs,cs);}
        else{Pen cp2(Color(255,55,55,55),1.f);g.DrawEllipse(&cp2,I(cx3+1),I(cy3+1),cs-2,cs-2);}
    }
    SelectObject(hdc,hflo);DeleteObject(hfl);
}

bool ClickFontPanel(POINT pt){
    if(!IsFontMode()||!PtInRect(&fpRect,pt))return false;
    int px=fpRect.left,py=fpRect.top,pw=FPW;
    int yy=py+26,ih=30;
    for(int i=0;i<(int)fps.size();i++){
        RECT ir={px+3,yy,px+pw-3,yy+ih-1};
        if(PtInRect(&ir,pt)){selFont=i;fname=fps[i].fn;fbold=fps[i].bold;fitalic=fps[i].italic;
            ApplyFontToSel();return true;}yy+=ih;}
    yy+=12;
    int slx=px+8,slw=pw-16;
    RECT slr2={slx,yy+2,slx+slw,yy+14};
    if(PtInRect(&slr2,pt)){float t3=(float)(pt.x-slx)/(float)slw;t3=max(0.f,min(1.f,t3));
        fsize=8.f+t3*(150.f-8.f);ApplyFontToSel();return true;}
    yy+=20+8;
    RECT bB={px+8,yy,px+54,yy+24},bI={px+60,yy,px+106,yy+24};
    if(PtInRect(&bB,pt)){fbold=!fbold;ApplyFontToSel();return true;}
    if(PtInRect(&bI,pt)){fitalic=!fitalic;ApplyFontToSel();return true;}
    yy+=32+6;
    int cs=26,cg=3,cc=6;
    for(int i=0;i<12;i++){
        int cx3=px+8+(i%cc)*(cs+cg),cy3=yy+(i/cc)*(cs+cg);
        RECT cr={cx3,cy3,cx3+cs,cy3+cs};
        if(PtInRect(&cr,pt)){curCol=fpCols[i];ApplyFontToSel();return true;}}
    return true; // поглощаем все клики в панели
}

void InitCBtns(int sx,int sy){
    Color pal[]={Color(255,255,0,0),Color(255,0,200,0),Color(255,30,144,255),
                 Color(255,255,200,0),Color(255,255,165,0),Color(255,255,255,255),
                 Color(255,0,0,0),Color(255,200,0,200)};
    cbns.clear();
    for(int i=0;i<8;i++){CBtn c;c.col=pal[i];
        c.rc={sx+i*(CW+2),sy,sx+i*(CW+2)+CW,sy+CW};cbns.push_back(c);}
}

void DrawToolbar(HDC hdc,int W){
    int nc=(int)tbns.size(),tw=nc*BW,cw2=8*(CW+2),tot=tw+14+cw2+CW+14;
    int sx=(W-tot)/2,sy=7;
    RECT bgR={sx-6,sy-5,sx+tot+6,sy+BH+10};
    HBRUSH bgb=CreateSolidBrush(RGB(26,26,26));FillRect(hdc,&bgR,bgb);DeleteObject(bgb);
    Graphics g(hdc);g.SetSmoothingMode(SmoothingModeAntiAlias);
    Pen bp2(Color(255,62,62,62),1.f);
    g.DrawRectangle(&bp2,I(bgR.left),I(bgR.top),I(bgR.right-bgR.left),I(bgR.bottom-bgR.top));
    for(int i=0;i<nc;i++)tbns[i].rc={sx+i*BW,sy,sx+i*BW+BW,sy+BH};
    int cx2=sx+tw+14,cy2=sy+(BH-CW)/2;InitCBtns(cx2,cy2);
    for(int i=0;i<nc;i++){
        auto& b=tbns[i];bool act=(curTool==b.tool);
        if(act){SolidBrush ab(Color(255,45,105,185));
            g.FillRectangle(&ab,I(b.rc.left),I(b.rc.top),I(b.rc.right-b.rc.left),I(b.rc.bottom-b.rc.top));}
        INT cx=(b.rc.left+b.rc.right)/2,cy=(b.rc.top+b.rc.bottom)/2;
        Color ic=act?Color(255,255,255,255):Color(255,140,140,140);
        Pen pi(ic,2.f);
        if(b.tool==Tool::SelectRegion){REAL dd[]={5,3};pi.SetDashPattern(dd,2);g.DrawRectangle(&pi,I(cx-13),I(cy-10),26,20);}
        else if(b.tool==Tool::Arrow){Obj tmp;tmp.type=ObjType::Arrow;tmp.color=ic;tmp.thick=2.5f;
            tmp.ax=(float)(cx-11);tmp.ay=(float)(cy+9);tmp.ax2=(float)(cx+11);tmp.ay2=(float)(cy-9);
            tmp.arrowSt=curArrow;DrawArrow(g,tmp);}
        else if(b.tool==Tool::Rectangle)g.DrawRectangle(&pi,I(cx-12),I(cy-8),24,16);
        else if(b.tool==Tool::Circle)g.DrawEllipse(&pi,I(cx-12),I(cy-9),24,18);
        else if(b.tool==Tool::Blur){for(int r=1;r<=3;r++){
            Color bc((BYTE)(60+r*50),ic.GetR(),ic.GetG(),ic.GetB());
            Pen bp3(bc,(REAL)(r*.8f));g.DrawEllipse(&bp3,I(cx-r*5),I(cy-r*5),r*10,r*10);}}
        else if(b.tool==Tool::Text){
            SetBkMode(hdc,TRANSPARENT);SetTextColor(hdc,act?RGB(255,255,255):RGB(140,140,140));
            HFONT hf=CreateFontW(22,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
            HFONT hfo=(HFONT)SelectObject(hdc,hf);RECT tr=b.rc;
            DrawTextW(hdc,L"T",-1,&tr,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            SelectObject(hdc,hfo);DeleteObject(hf);}
        else if(b.tool==Tool::Brush){Pen bp4(ic,2.2f);bp4.SetStartCap(LineCapRound);bp4.SetEndCap(LineCapRound);
            g.DrawLine(&bp4,I(cx-10),I(cy+9),I(cx+6),I(cy-9));
            SolidBrush bb(ic);g.FillEllipse(&bb,I(cx-13),I(cy+6),7,7);}
        else if(b.tool==Tool::Close){Pen xp(Color(255,200,65,65),2.5f);
            g.DrawLine(&xp,I(cx-8),I(cy-8),I(cx+8),I(cy+8));
            g.DrawLine(&xp,I(cx+8),I(cy-8),I(cx-8),I(cy+8));}
        if(i<nc-1){Pen dp(Color(255,45,45,45),1.f);
            g.DrawLine(&dp,I(b.rc.right),I(b.rc.top+5),I(b.rc.right),I(b.rc.bottom-5));}
    }
    Pen dp2(Color(255,50,50,50),1.f);g.DrawLine(&dp2,I(cx2-7),I(sy+4),I(cx2-7),I(sy+BH-4));
    for(auto& cb:cbns){bool act=(cb.col.GetValue()==curCol.GetValue());
        SolidBrush br(cb.col);g.FillEllipse(&br,I(cb.rc.left+2),I(cb.rc.top+2),CW-4,CW-4);
        Pen ep(act?Color(255,255,255,255):Color(255,55,55,55),act?2.f:1.f);
        g.DrawEllipse(&ep,I(cb.rc.left+(act?1:2)),I(cb.rc.top+(act?1:2)),CW-(act?2:4),CW-(act?2:4));}
    {int ix=cx2+8*(CW+2)+4,iy=sy+(BH-CW)/2;
     SolidBrush ib(curCol);g.FillEllipse(&ib,I(ix),I(iy),CW+6,CW+6);
     Pen wp(Color(255,255,255,255),1.5f);g.DrawEllipse(&wp,I(ix),I(iy),CW+6,CW+6);}
}

void DrawPropBar(HDC hdc,int W,int H){
    int by=H-PH;RECT br={0,by,W,H};
    HBRUSH bg=CreateSolidBrush(RGB(20,20,20));FillRect(hdc,&br,bg);DeleteObject(bg);
    HPEN lp2=CreatePen(PS_SOLID,1,RGB(45,45,45));HPEN lo=(HPEN)SelectObject(hdc,lp2);
    MoveToEx(hdc,0,by,NULL);LineTo(hdc,W,by);SelectObject(hdc,lo);DeleteObject(lp2);
    SetBkMode(hdc,TRANSPARENT);
    HFONT hf=CreateFontW(11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    HFONT ho=(HFONT)SelectObject(hdc,hf);SetTextColor(hdc,RGB(120,120,120));
    TextOutW(hdc,8,by+8,L"Толщина:",8);
    RECT slr2={76,by+11,166,by+19};HBRUSH slb=CreateSolidBrush(RGB(45,45,45));FillRect(hdc,&slr2,slb);DeleteObject(slb);
    float t=(curThick-1.f)/9.f;int sp=(int)(t*88);
    RECT slh={76+sp-3,by+7,76+sp+3,by+23};HBRUSH slhb=CreateSolidBrush(RGB(80,150,230));FillRect(hdc,&slh,slhb);DeleteObject(slhb);
    wchar_t buf[64];swprintf_s(buf,L"%.1f",curThick);TextOutW(hdc,170,by+8,buf,(int)wcslen(buf));
    if(curTool==Tool::Arrow){
        TextOutW(hdc,200,by+8,L"Стиль:",6);
        const wchar_t* ns[]={L"Filled",L"Outline",L"Double",L"Line"};
        ArrowStyle as[]={ArrowStyle::Normal,ArrowStyle::Outline,ArrowStyle::Double,ArrowStyle::Line};
        for(int i=0;i<4;i++){bool act=(curArrow==as[i]);
            RECT ab={258+i*68,by+4,258+i*68+64,by+28};
            HBRUSH abb=CreateSolidBrush(act?RGB(50,105,185):RGB(36,36,36));FillRect(hdc,&ab,abb);DeleteObject(abb);
            SetTextColor(hdc,act?RGB(255,255,255):RGB(110,110,110));
            DrawTextW(hdc,ns[i],-1,&ab,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            SetTextColor(hdc,RGB(120,120,120));}}
    if(curTool==Tool::Blur){
        TextOutW(hdc,200,by+8,L"Радиус:",7);
        RECT sr={262,by+11,342,by+19};HBRUSH sb=CreateSolidBrush(RGB(45,45,45));FillRect(hdc,&sr,sb);DeleteObject(sb);
        float t2=(float)(blurR-1)/49.f;int sp2=(int)(t2*78);
        RECT sh={262+sp2-3,by+7,262+sp2+3,by+23};HBRUSH shb=CreateSolidBrush(RGB(80,200,150));FillRect(hdc,&sh,shb);DeleteObject(shb);
        swprintf_s(buf,L"%d",blurR);TextOutW(hdc,346,by+8,buf,(int)wcslen(buf));}
    if(curTool==Tool::Brush){swprintf_s(buf,L"Кисть: %d px",brushSz);TextOutW(hdc,200,by+8,buf,(int)wcslen(buf));}
    if(editing){
        SetTextColor(hdc,RGB(80,160,255));
        TextOutW(hdc,200,by+8,L"● Ввод текста  [Enter]—новая строка  [Esc]—завершить",50);}
    SetTextColor(hdc,RGB(50,50,50));RECT tip={0,by,W-6,H};
    DrawTextW(hdc,L"Колесо — размер   Ctrl+Z — отмена   Enter — сохранить   Del — удалить",
              -1,&tip,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(hdc,ho);DeleteObject(hf);
}

void DrawDim(HDC hdc){
    Graphics g(hdc);SolidBrush dim(Color(150,0,0,0));
    if(!regSel&&!drawing){g.FillRectangle(&dim,I(0),I(0),I(SW),I(SH));return;}
    RECT sel;
    if(drawing&&curTool==Tool::SelectRegion)sel=NR(mStart,mEnd);else sel=regRect;
    g.FillRectangle(&dim,I(0),I(0),I(SW),I(sel.top));
    g.FillRectangle(&dim,I(0),I(sel.bottom),I(SW),I(SH-sel.bottom));
    g.FillRectangle(&dim,I(0),I(sel.top),I(sel.left),I(sel.bottom-sel.top));
    g.FillRectangle(&dim,I(sel.right),I(sel.top),I(SW-sel.right),I(sel.bottom-sel.top));
    Pen sp(Color(255,255,255,255),1.5f);REAL d[]={6,3};sp.SetDashPattern(d,2);
    g.DrawRectangle(&sp,I(sel.left),I(sel.top),I(sel.right-sel.left),I(sel.bottom-sel.top));
    Pen cp3(Color(255,80,160,255),3.f);int cs=12;
    auto C=[&](int x,int y,int dx,int dy){g.DrawLine(&cp3,I(x),I(y+dy*cs),I(x),I(y));g.DrawLine(&cp3,I(x),I(y),I(x+dx*cs),I(y));};
    C(sel.left,sel.top,1,1);C(sel.right,sel.top,-1,1);C(sel.left,sel.bottom,1,-1);C(sel.right,sel.bottom,-1,-1);
    wchar_t sz[64];swprintf_s(sz,L" %ld×%ld px ",sel.right-sel.left,sel.bottom-sel.top);
    Font f(L"Segoe UI",10.f);SolidBrush wb(Color(255,255,255,255)),db(Color(200,20,20,20));
    REAL py2=(REAL)(sel.top-24<0?sel.bottom+4:sel.top-24);PointF pos((REAL)(sel.left+4),py2);
    RectF mr;g.MeasureString(sz,-1,&f,pos,&mr);g.FillRectangle(&db,mr);g.DrawString(sz,-1,&f,pos,&wb);
}

void Capture(){
    if(hScr){DeleteObject(hScr);hScr=NULL;}
    HDC s=GetDC(NULL),m=CreateCompatibleDC(s);
    hScr=CreateCompatibleBitmap(s,SW,SH);SelectObject(m,hScr);
    BitBlt(m,0,0,SW,SH,s,0,0,SRCCOPY);DeleteDC(m);ReleaseDC(NULL,s);
}

std::wstring SaveDlg(HWND hw){
    wchar_t fn[MAX_PATH]=L"screenshot.jpg";OPENFILENAMEW o={sizeof(o)};
    o.hwndOwner=hw;o.lpstrFilter=L"JPEG\0*.jpg\0PNG\0*.png\0\0";
    o.lpstrFile=fn;o.nMaxFile=MAX_PATH;o.lpstrDefExt=L"jpg";o.Flags=OFN_OVERWRITEPROMPT;
    if(GetSaveFileNameW(&o))return fn;return L"";}

HBITMAP MakeFinal(){
    RECT reg=regRect;int w=(int)(reg.right-reg.left),h=(int)(reg.bottom-reg.top);
    if(w<=0||h<=0){reg={0,0,SW,SH};w=SW;h=SH;}
    HDC sc=GetDC(NULL),mc=CreateCompatibleDC(sc);
    HBITMAP hf=CreateCompatibleBitmap(sc,w,h);HGDIOBJ ho=SelectObject(mc,hf);
    HDC bg=CreateCompatibleDC(sc);SelectObject(bg,hScr);
    BitBlt(mc,0,0,w,h,bg,reg.left,reg.top,SRCCOPY);DeleteDC(bg);
    for(auto& o:objs)if(o.type==ObjType::Blur)
        ApplyBlur(mc,hScr,I(o.x)-reg.left,I(o.y)-reg.top,I(o.w),I(o.h),o.blurR,o.blurA);
    Graphics g(mc);g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.TranslateTransform((REAL)-reg.left,(REAL)-reg.top);
    for(auto& o:objs)if(o.type!=ObjType::Blur)DrawObj(g,o,mc);
    SelectObject(mc,ho);DeleteDC(mc);ReleaseDC(NULL,sc);return hf;}

void DoSave(HWND hw){
    StopTextEdit();
    HBITMAP hf=MakeFinal();CopyToClip(hw,hf);
    if(MessageBoxW(hw,L"Скопировано!\n\nСохранить в файл?",L"OK",MB_YESNO|MB_ICONINFORMATION)==IDYES){
        std::wstring p=SaveDlg(hw);
        if(!p.empty()){std::wstring e=p.size()>=4?p.substr(p.size()-4):L"";
            for(auto& c:e)c=(wchar_t)towlower(c);
            const WCHAR* m2=(e==L".png")?L"image/png":L"image/jpeg";
            CLSID cl;GetClsid(m2,&cl);Bitmap* bmp=new Bitmap(hf,NULL);
            if(e!=L".png"){EncoderParameters ep;ep.Count=1;
                ep.Parameter[0].Guid=EncoderQuality;ep.Parameter[0].Type=EncoderParameterValueTypeLong;
                ep.Parameter[0].NumberOfValues=1;ULONG q=95;ep.Parameter[0].Value=&q;
                bmp->Save(p.c_str(),&cl,&ep);}
            else bmp->Save(p.c_str(),&cl,NULL);
            delete bmp;MessageBoxW(hw,(L"Сохранено:\n"+p).c_str(),L"OK",MB_OK);}}
    DeleteObject(hf);}

static bool IsOverUI(POINT pt){
    for(auto& b:tbns)if(PtInRect(&b.rc,pt))return true;
    for(auto& c:cbns)if(PtInRect(&c.rc,pt))return true;
    if(pt.y<TH+4)return true;
    if(pt.y>=SH-PH)return true;
    if(IsFontMode()&&PtInRect(&fpRect,pt))return true;
    return false;
}

LRESULT CALLBACK OvProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp){
    auto MP=[&]()->POINT{return{(int)(short)LOWORD(lp),(int)(short)HIWORD(lp)};};
    auto InPB=[&](POINT pt)->bool{return pt.y>=SH-PH;};

    switch(msg){
    case WM_TIMER:
        if(wp==CURSOR_TIMER){cursorVis=!cursorVis;InvalidateRect(hw,NULL,FALSE);}
        break;

    case WM_PAINT:{
        PAINTSTRUCT ps;HDC hdc=BeginPaint(hw,&ps);
        HDC buf=CreateCompatibleDC(hdc);
        HBITMAP hbm=CreateCompatibleBitmap(hdc,SW,SH);
        HGDIOBJ ho=SelectObject(buf,hbm);
        HDC bg=CreateCompatibleDC(hdc);SelectObject(bg,hScr);
        BitBlt(buf,0,0,SW,SH,bg,0,0,SRCCOPY);DeleteDC(bg);
        for(auto& o:objs)if(o.type==ObjType::Blur)
            ApplyBlur(buf,hScr,I(o.x),I(o.y),I(o.w),I(o.h),o.blurR,o.blurA);
        DrawDim(buf);
        {Graphics g(buf);g.SetSmoothingMode(SmoothingModeAntiAlias);
         for(auto& o:objs)if(o.type!=ObjType::Blur)DrawObj(g,o,buf,cursorVis);
         if(drawing){
             Obj t;t.color=curCol;t.thick=curThick;t.arrowSt=curArrow;
             RECT r=NR(mStart,mEnd);
             if(curTool==Tool::Arrow){t.type=ObjType::Arrow;t.ax=(float)mStart.x;t.ay=(float)mStart.y;t.ax2=(float)mEnd.x;t.ay2=(float)mEnd.y;DrawObj(g,t,NULL);}
             else if(curTool==Tool::Rectangle){t.type=ObjType::Rectangle;t.x=(float)r.left;t.y=(float)r.top;t.w=(float)(r.right-r.left);t.h=(float)(r.bottom-r.top);DrawObj(g,t,NULL);}
             else if(curTool==Tool::Circle){t.type=ObjType::Circle;t.x=(float)r.left;t.y=(float)r.top;t.w=(float)(r.right-r.left);t.h=(float)(r.bottom-r.top);DrawObj(g,t,NULL);}
             else if(curTool==Tool::Blur){Pen pp(Color(200,120,200,255),1.5f);REAL dd[]={4,3};pp.SetDashPattern(dd,2);
                 g.DrawRectangle(&pp,I(r.left),I(r.top),I(r.right-r.left),I(r.bottom-r.top));}}
         if(curTool==Tool::Brush){POINT c;GetCursorPos(&c);ScreenToClient(hw,&c);
             Pen cp4(Color(180,255,255,255),1.f);g.DrawEllipse(&cp4,I(c.x-brushSz),I(c.y-brushSz),brushSz*2,brushSz*2);}
        }
        DrawToolbar(buf,SW);DrawPropBar(buf,SW,SH);DrawFontPanel(buf,SW,SH);
        BitBlt(hdc,0,0,SW,SH,buf,0,0,SRCCOPY);
        SelectObject(buf,ho);DeleteObject(hbm);DeleteDC(buf);
        EndPaint(hw,&ps);break;}

    case WM_LBUTTONDOWN:{
        POINT pt=MP();
        // Тулбар
        for(auto& b:tbns){if(PtInRect(&b.rc,pt)){
            if(b.tool==Tool::Close){StopTextEdit();DestroyWindow(hw);return 0;}
            if(editing)StopTextEdit();
            curTool=b.tool;
            for(auto& o:objs)o.selected=false;selIdx=-1;
            InvalidateRect(hw,NULL,FALSE);return 0;}}
        for(auto& c:cbns){if(PtInRect(&c.rc,pt)){
            curCol=c.col;
            if(selIdx>=0&&selIdx<(int)objs.size())objs[selIdx].color=curCol;
            if(editing&&editIdx>=0&&editIdx<(int)objs.size())objs[editIdx].color=curCol;
            InvalidateRect(hw,NULL,FALSE);return 0;}}
        if(pt.y<TH+4)return 0;
        // PropBar
        if(InPB(pt)){
            int by=SH-PH;
            if(pt.x>=76&&pt.x<=166&&pt.y>=by+7&&pt.y<=by+23){
                float t=(float)(pt.x-76)/88.f;curThick=1.f+t*9.f;
                if(selIdx>=0&&selIdx<(int)objs.size())objs[selIdx].thick=curThick;}
            if(curTool==Tool::Arrow){
                ArrowStyle as[]={ArrowStyle::Normal,ArrowStyle::Outline,ArrowStyle::Double,ArrowStyle::Line};
                for(int i=0;i<4;i++){RECT ab={258+i*68,by+4,258+i*68+64,by+28};if(PtInRect(&ab,pt)){curArrow=as[i];break;}}}
            if(curTool==Tool::Blur&&pt.x>=262&&pt.x<=342&&pt.y>=by+7&&pt.y<=by+23){
                float t2=(float)(pt.x-262)/78.f;blurR=1+(int)(t2*49);
                if(selIdx>=0&&selIdx<(int)objs.size()&&objs[selIdx].type==ObjType::Blur)objs[selIdx].blurR=blurR;}
            InvalidateRect(hw,NULL,FALSE);return 0;}
        // Font panel — поглощает всё
        if(IsFontMode()&&PtInRect(&fpRect,pt)){
            ClickFontPanel(pt);
            if(editing){SetFocus(hw);SetForegroundWindow(hw);}
            InvalidateRect(hw,NULL,FALSE);return 0;}

        // --- Холст ---

        // Text tool
        if(curTool==Tool::Text){
            // Кнопка удаления активного текста
            if(selIdx>=0&&selIdx<(int)objs.size()&&objs[selIdx].type==ObjType::Text&&objs[selIdx].selected){
                if(HitDelText(objs[selIdx],pt)){
                    if(editing&&editIdx==selIdx)StopTextEdit();
                    else{objs.erase(objs.begin()+selIdx);selIdx=-1;}
                    InvalidateRect(hw,NULL,FALSE);return 0;}
                // Рамка — перетаскивание
                if(HitTextBorder(objs[selIdx],pt)){
                    dragging=true;mStart=pt;
                    ox=objs[selIdx].x;oy=objs[selIdx].y;
                    SetCapture(hw);
                    InvalidateRect(hw,NULL,FALSE);return 0;}
                // Внутри текста — выделение мышью
                if(HitObj(objs[selIdx],pt)&&editing&&editIdx==selIdx){
                    HDC dc=GetDC(hw);Graphics g(dc);
                    int pos=GetCharPosFromPoint(g,objs[selIdx],pt);
                    ReleaseDC(hw,dc);
                    cursorPos=pos;
                    textSelStart=pos;textSelEnd=pos;
                    textSelecting=true;
                    SetCapture(hw);
                    cursorVis=true;InvalidateRect(hw,NULL,FALSE);return 0;}
            }
            // Клик на другой текстовый объект — выделяем и начинаем редактировать
            for(int i=(int)objs.size()-1;i>=0;i--){
                if(objs[i].type==ObjType::Text&&HitObj(objs[i],pt)){
                    if(editing)StopTextEdit();
                    if(selIdx>=0&&selIdx<(int)objs.size())objs[selIdx].selected=false;
                    selIdx=i;objs[i].selected=true;
                    fname=objs[i].font;fsize=objs[i].fsize;
                    fbold=objs[i].bold;fitalic=objs[i].italic;curCol=objs[i].color;
                    for(int j=0;j<(int)fps.size();j++)
                        if(fps[j].fn==fname&&fps[j].bold==fbold&&fps[j].italic==fitalic){selFont=j;break;}
                    StartTextEdit(i);
                    // Позиционируем курсор
                    HDC dc=GetDC(hw);Graphics g(dc);
                    cursorPos=GetCharPosFromPoint(g,objs[i],pt);
                    ReleaseDC(hw,dc);
                    textSelStart=cursorPos;textSelEnd=cursorPos;
                    textSelecting=true;SetCapture(hw);
                    InvalidateRect(hw,NULL,FALSE);return 0;}}
            // Пустое место — новый текстовый объект
            if(selIdx>=0&&selIdx<(int)objs.size())objs[selIdx].selected=false;
            if(editing)StopTextEdit();
            Obj o;o.type=ObjType::Text;o.x=(float)pt.x;o.y=(float)pt.y;
            o.color=curCol;o.font=fname;o.fsize=fsize;o.bold=fbold;o.italic=fitalic;
            o.text=L"";o.selected=true;
            objs.push_back(o);selIdx=(int)objs.size()-1;
            UpdateTextBBox(objs[selIdx]);
            StartTextEdit(selIdx);
            InvalidateRect(hw,NULL,FALSE);return 0;}

        // Brush
        if(curTool==Tool::Brush){
            brushing=true;
            Obj o;o.type=ObjType::Brush;o.color=curCol;o.thick=(float)brushSz;
            o.pts.push_back({(float)pt.x,(float)pt.y});
            if(selIdx>=0&&selIdx<(int)objs.size())objs[selIdx].selected=false;
            selIdx=-1;objs.push_back(o);SetCapture(hw);return 0;}

        // SelectRegion
        if(curTool==Tool::SelectRegion){drawing=true;mStart=mEnd=pt;return 0;}

        // Остальные — Arrow/Rect/Circle/Blur
        // Проверка кнопки удаления выделенного
        if(selIdx>=0&&selIdx<(int)objs.size()&&objs[selIdx].selected){
            if(HitDel(objs[selIdx],pt)){
                objs.erase(objs.begin()+selIdx);selIdx=-1;
                InvalidateRect(hw,NULL,FALSE);return 0;}
            // Маркеры
            int h2=HitH(objs[selIdx],pt);
            if(h2>=0){
                if(h2==8){rotating=true;oangle=objs[selIdx].angle;mStart=pt;}
                else{resizing=true;dragH=h2;mStart=pt;
                    ox=objs[selIdx].x;oy=objs[selIdx].y;
                    ow=objs[selIdx].w;oh=objs[selIdx].h;
                    oax=objs[selIdx].ax;oay=objs[selIdx].ay;
                    oax2=objs[selIdx].ax2;oay2=objs[selIdx].ay2;}
                SetCapture(hw);return 0;}
            // Внутри объекта — перетаскивание
            if(HitObj(objs[selIdx],pt)){
                dragging=true;mStart=pt;
                ox=objs[selIdx].x;oy=objs[selIdx].y;
                oax=objs[selIdx].ax;oay=objs[selIdx].ay;
                oax2=objs[selIdx].ax2;oay2=objs[selIdx].ay2;
                SetCapture(hw);return 0;}}
        // Поиск объекта под курсором
        {bool found=false;
        for(int i=(int)objs.size()-1;i>=0;i--){
            if(HitObj(objs[i],pt)){
                if(selIdx>=0&&selIdx<(int)objs.size())objs[selIdx].selected=false;
                selIdx=i;objs[i].selected=true;
                dragging=true;mStart=pt;
                ox=objs[i].x;oy=objs[i].y;
                oax=objs[i].ax;oay=objs[i].ay;
                oax2=objs[i].ax2;oay2=objs[i].ay2;
                SetCapture(hw);found=true;InvalidateRect(hw,NULL,FALSE);return 0;}}
        if(!found){
            if(selIdx>=0&&selIdx<(int)objs.size()){objs[selIdx].selected=false;selIdx=-1;}
            drawing=true;mStart=mEnd=pt;}}
        break;}

    case WM_MOUSEMOVE:{
        POINT pt=MP();
        // Выделение текста мышью
        if(textSelecting&&editing&&editIdx>=0&&editIdx<(int)objs.size()){
            HDC dc=GetDC(hw);Graphics g(dc);
            int pos=GetCharPosFromPoint(g,objs[editIdx],pt);
            ReleaseDC(hw,dc);
            textSelEnd=pos;cursorPos=pos;
            cursorVis=true;InvalidateRect(hw,NULL,FALSE);break;}
        if(dragging&&selIdx>=0&&selIdx<(int)objs.size()){
            float dx=(float)(pt.x-mStart.x),dy=(float)(pt.y-mStart.y);
            objs[selIdx].x=ox+dx;objs[selIdx].y=oy+dy;
            objs[selIdx].ax=oax+dx;objs[selIdx].ay=oay+dy;
            objs[selIdx].ax2=oax2+dx;objs[selIdx].ay2=oay2+dy;
            if(objs[selIdx].type==ObjType::Brush){
                static float lastDx=0,lastDy=0;
                float ddx=dx-lastDx,ddy=dy-lastDy;
                for(auto& bp:objs[selIdx].pts){bp.x+=ddx;bp.y+=ddy;}
                lastDx=dx;lastDy=dy;}
            InvalidateRect(hw,NULL,FALSE);break;}
        if(resizing&&selIdx>=0&&selIdx<(int)objs.size()){DoResize(objs[selIdx],dragH,pt);InvalidateRect(hw,NULL,FALSE);break;}
        if(rotating&&selIdx>=0&&selIdx<(int)objs.size()){
            auto& o=objs[selIdx];float cx2=o.x+o.w/2,cy2=o.y+o.h/2;
            float a1=atan2f((float)(mStart.y-cy2),(float)(mStart.x-cx2));
            float a2=atan2f((float)(pt.y-cy2),(float)(pt.x-cx2));
            o.angle=oangle+(a2-a1)*180.f/(float)M_PI;InvalidateRect(hw,NULL,FALSE);break;}
        if(brushing&&!objs.empty()){objs.back().pts.push_back({(float)pt.x,(float)pt.y});InvalidateRect(hw,NULL,FALSE);break;}
        if(!drawing)break;mEnd=pt;InvalidateRect(hw,NULL,FALSE);break;}

    case WM_LBUTTONUP:{
        POINT pt=MP();ReleaseCapture();
        if(textSelecting){
            textSelecting=false;
            // Если не было реального выделения — сбрасываем
            if(textSelStart==textSelEnd){textSelStart=textSelEnd=-1;}
            break;}
        if(dragging){dragging=false;break;}
        if(resizing){resizing=false;break;}
        if(rotating){rotating=false;break;}
        if(brushing){brushing=false;break;}
        if(!drawing)break;drawing=false;mEnd=pt;
        if(curTool==Tool::SelectRegion){
            regRect=NR(mStart,mEnd);
            if(regRect.right-regRect.left>5&&regRect.bottom-regRect.top>5){regSel=true;curTool=Tool::Arrow;}
        } else {
            RECT r=NR(mStart,mEnd);int w2=r.right-r.left,h2=r.bottom-r.top;
            if(w2<3&&h2<3)break;
            Obj o;o.color=curCol;o.thick=curThick;o.arrowSt=curArrow;o.selected=true;
            if(curTool==Tool::Arrow){o.type=ObjType::Arrow;o.ax=(float)mStart.x;o.ay=(float)mStart.y;o.ax2=(float)mEnd.x;o.ay2=(float)mEnd.y;}
            else if(curTool==Tool::Rectangle){o.type=ObjType::Rectangle;o.x=(float)r.left;o.y=(float)r.top;o.w=(float)w2;o.h=(float)h2;}
            else if(curTool==Tool::Circle){o.type=ObjType::Circle;o.x=(float)r.left;o.y=(float)r.top;o.w=(float)w2;o.h=(float)h2;}
            else if(curTool==Tool::Blur){o.type=ObjType::Blur;o.x=(float)r.left;o.y=(float)r.top;o.w=(float)w2;o.h=(float)h2;o.blurR=blurR;o.blurA=blurA;}
            if(selIdx>=0&&selIdx<(int)objs.size())objs[selIdx].selected=false;
            objs.push_back(o);selIdx=(int)objs.size()-1;}
        InvalidateRect(hw,NULL,FALSE);break;}

    case WM_CHAR:
        if(editing&&editIdx>=0&&editIdx<(int)objs.size()){
            wchar_t ch=(wchar_t)wp;
            if(ch=='\r'||ch=='\n'){TextInsert(L'\n');}
            else if(ch==8){TextBackspace();}
            else if(ch>=32){TextInsert(ch);}
            return 0;}
        break;

    case WM_KEYDOWN:
        if(editing){
            bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
            switch(wp){
            case VK_ESCAPE: StopTextEdit();InvalidateRect(hw,NULL,FALSE);break;
            case VK_LEFT:
                if(cursorPos>0)cursorPos--;
                if(shift){if(textSelStart<0)textSelStart=cursorPos+1;textSelEnd=cursorPos;}
                else{textSelStart=textSelEnd=-1;}
                cursorVis=true;InvalidateRect(hw,NULL,FALSE);break;
            case VK_RIGHT:
                if(editIdx>=0&&cursorPos<(int)objs[editIdx].text.size())cursorPos++;
                if(shift){if(textSelStart<0)textSelStart=cursorPos-1;textSelEnd=cursorPos;}
                else{textSelStart=textSelEnd=-1;}
                cursorVis=true;InvalidateRect(hw,NULL,FALSE);break;
            case VK_HOME:
                cursorPos=0;
                if(!shift)textSelStart=textSelEnd=-1;
                else{if(textSelStart<0)textSelStart=cursorPos+1;textSelEnd=0;}
                cursorVis=true;InvalidateRect(hw,NULL,FALSE);break;
            case VK_END:
                if(editIdx>=0)cursorPos=(int)objs[editIdx].text.size();
                if(!shift)textSelStart=textSelEnd=-1;
                cursorVis=true;InvalidateRect(hw,NULL,FALSE);break;
            case VK_DELETE:TextDelete();break;
            case VK_BACK:  TextBackspace();break;
            case 'A':
                if(GetKeyState(VK_CONTROL)&0x8000){
                    // Выделить всё
                    if(editIdx>=0&&editIdx<(int)objs.size()){
                        textSelStart=0;textSelEnd=(int)objs[editIdx].text.size();
                        cursorPos=textSelEnd;}
                    cursorVis=true;InvalidateRect(hw,NULL,FALSE);}
                break;
            case 'C':
                if((GetKeyState(VK_CONTROL)&0x8000)&&textSelStart>=0&&textSelEnd>=0&&textSelStart!=textSelEnd){
                    int a=min(textSelStart,textSelEnd),b=max(textSelStart,textSelEnd);
                    std::wstring sel=objs[editIdx].text.substr(a,b-a);
                    if(OpenClipboard(hw)){EmptyClipboard();
                        HANDLE h=GlobalAlloc(GMEM_MOVEABLE,(sel.size()+1)*2);
                        if(h){auto* p=(wchar_t*)GlobalLock(h);wcscpy_s(p,sel.size()+1,sel.c_str());
                            GlobalUnlock(h);SetClipboardData(CF_UNICODETEXT,h);}
                        CloseClipboard();}}
                break;
            case 'V':
                if(GetKeyState(VK_CONTROL)&0x8000){
                    if(OpenClipboard(hw)){
                        HANDLE h=GetClipboardData(CF_UNICODETEXT);
                        if(h){auto* p=(wchar_t*)GlobalLock(h);
                            if(p){for(wchar_t* c=p;*c;c++)if(*c>=32||*c==L'\n')TextInsert(*c);}
                            GlobalUnlock(h);}
                        CloseClipboard();}}
                break;
            }
            break;
        }
        if(wp==VK_RETURN){DoSave(hw);DestroyWindow(hw);}
        if(wp==VK_ESCAPE){
            if(editing){StopTextEdit();InvalidateRect(hw,NULL,FALSE);}
            else if(selIdx>=0&&selIdx<(int)objs.size()){objs[selIdx].selected=false;selIdx=-1;InvalidateRect(hw,NULL,FALSE);}
            else DestroyWindow(hw);}
        if(wp==VK_DELETE&&!editing&&selIdx>=0&&selIdx<(int)objs.size()){
            objs.erase(objs.begin()+selIdx);selIdx=-1;InvalidateRect(hw,NULL,FALSE);}
        if(wp=='Z'&&(GetKeyState(VK_CONTROL)&0x8000)){
            if(!editing){
                if(selIdx>=(int)objs.size()-1)selIdx=-1;
                if(!objs.empty())objs.pop_back();
                else if(regSel){regSel=false;curTool=Tool::SelectRegion;}
                InvalidateRect(hw,NULL,FALSE);}}
        {bool plus=(wp==VK_OEM_PLUS||wp==VK_ADD),minus=(wp==VK_OEM_MINUS||wp==VK_SUBTRACT);
         if(plus||minus){int dir=plus?1:-1;
             if(curTool==Tool::Brush)brushSz=max(1,min(100,brushSz+dir*2));
             else if(curTool==Tool::Blur)blurR=max(1,min(50,blurR+dir*2));
             else if(IsFontMode()){fsize=max(8.f,min(150.f,fsize+dir*2));ApplyFontToSel();}
             else curThick=max(1.f,min(20.f,curThick+dir*.5f));
             InvalidateRect(hw,NULL,FALSE);}}
        break;

    case WM_MOUSEWHEEL:{
        int dir=GET_WHEEL_DELTA_WPARAM(wp)>0?1:-1;
        if(curTool==Tool::Brush)brushSz=max(1,min(100,brushSz+dir*2));
        else if(curTool==Tool::Blur)blurR=max(1,min(50,blurR+dir*2));
        else if(IsFontMode()){fsize=max(8.f,min(150.f,fsize+dir*2));ApplyFontToSel();}
        else curThick=max(1.f,min(20.f,curThick+dir*.5f));
        InvalidateRect(hw,NULL,FALSE);break;}

    case WM_SETCURSOR:{
        POINT pt;GetCursorPos(&pt);ScreenToClient(hw,&pt);
        if(IsOverUI(pt)){SetCursor(LoadCursor(NULL,IDC_ARROW));return TRUE;}
        if(curTool==Tool::Text){
            // Внутри текстового объекта в режиме редактирования — ibeam
            if(editing&&editIdx>=0&&editIdx<(int)objs.size()&&HitObj(objs[editIdx],pt)){
                SetCursor(LoadCursor(NULL,IDC_IBEAM));return TRUE;}
            // Рамка выделенного текста — move
            if(selIdx>=0&&selIdx<(int)objs.size()&&objs[selIdx].type==ObjType::Text&&objs[selIdx].selected){
                if(HitTextBorder(objs[selIdx],pt)){SetCursor(LoadCursor(NULL,IDC_SIZEALL));return TRUE;}}
            SetCursor(LoadCursor(NULL,IDC_IBEAM));return TRUE;}
        if(curTool==Tool::SelectRegion&&!regSel){SetCursor(LoadCursor(NULL,IDC_CROSS));return TRUE;}
        if(curTool==Tool::Brush){SetCursor(LoadCursor(NULL,IDC_CROSS));return TRUE;}
        if(selIdx>=0&&selIdx<(int)objs.size()&&objs[selIdx].selected){
            int h2=HitH(objs[selIdx],pt);
            if(h2==8)SetCursor(LoadCursor(NULL,IDC_SIZEALL));
            else if(h2>=0)SetCursor(LoadCursor(NULL,IDC_SIZENWSE));
            else if(HitObj(objs[selIdx],pt))SetCursor(LoadCursor(NULL,IDC_SIZEALL));
            else SetCursor(LoadCursor(NULL,IDC_ARROW));
            return TRUE;}
        SetCursor(LoadCursor(NULL,IDC_ARROW));return TRUE;}

    case WM_DESTROY:StopTextEdit();hOver=NULL;break;
    default:return DefWindowProc(hw,msg,wp,lp);
    }return 0;
}

void OpenOverlay(){
    if(hOver){SetForegroundWindow(hOver);return;}
    objs.clear();selIdx=-1;
    drawing=dragging=resizing=rotating=brushing=editing=textSelecting=false;
    textSelStart=textSelEnd=-1;
    regSel=false;curTool=Tool::SelectRegion;regRect={0,0,0,0};
    Capture();
    hOver=CreateWindowExW(WS_EX_TOPMOST,L"SnipOv",L"",WS_POPUP|WS_VISIBLE,
        GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),
        SW,SH,NULL,NULL,hInst,NULL);
    SetForegroundWindow(hOver);
}

LRESULT CALLBACK TrayProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_HOTKEY:if(wp==HOTKEY_ID)OpenOverlay();break;
    case WM_REGHOT:UnregisterHotKey(hw,HOTKEY_ID);RegisterHotKey(hw,HOTKEY_ID,HOTKEY_MOD,HOTKEY_KEY);break;
    case WM_TRAYICON:
        if(lp==WM_RBUTTONUP){POINT pt;GetCursorPos(&pt);
            HMENU hm=CreatePopupMenu();
            AppendMenuW(hm,MF_STRING,ID_TRAY_OPEN,L"Скриншот (Ctrl+Q)");
            AppendMenuW(hm,MF_SEPARATOR,0,NULL);
            AppendMenuW(hm,MF_STRING,ID_TRAY_EXIT,L"Выход");
            SetForegroundWindow(hw);TrackPopupMenu(hm,TPM_RIGHTBUTTON,pt.x,pt.y,0,hw,NULL);DestroyMenu(hm);}
        if(lp==WM_LBUTTONDBLCLK)OpenOverlay();break;
    case WM_COMMAND:
        if(LOWORD(wp)==ID_TRAY_EXIT)PostQuitMessage(0);
        if(LOWORD(wp)==ID_TRAY_OPEN)OpenOverlay();break;
    case WM_DESTROY:Shell_NotifyIconW(NIM_DELETE,&nid);PostQuitMessage(0);break;
    default:return DefWindowProc(hw,msg,wp,lp);}return 0;
}
VOID CALLBACK ReHot(HWND,UINT,UINT_PTR,DWORD){if(hMain)PostMessage(hMain,WM_REGHOT,0,0);}

int WINAPI WinMain(HINSTANCE hi,HINSTANCE,LPSTR,int){
    hInst=hi;
    GdiplusStartupInput gsi;ULONG_PTR tok;GdiplusStartup(&tok,&gsi,NULL);
    SW=GetSystemMetrics(SM_CXVIRTUALSCREEN);SH=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(SW<=0)SW=GetSystemMetrics(SM_CXSCREEN);if(SH<=0)SH=GetSystemMetrics(SM_CYSCREEN);
    tbns={{Tool::SelectRegion,{}},{Tool::Arrow,{}},{Tool::Rectangle,{}},
          {Tool::Circle,{}},{Tool::Blur,{}},{Tool::Text,{}},{Tool::Brush,{}},{Tool::Close,{}}};
    WNDCLASSEXW wc={sizeof(wc),CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS,OvProc,0,0,hInst,
        NULL,LoadCursor(NULL,IDC_CROSS),NULL,NULL,L"SnipOv",NULL};
    RegisterClassExW(&wc);
    WNDCLASSEXW wt={sizeof(wt),0,TrayProc,0,0,hInst,NULL,NULL,NULL,NULL,L"SnipTr",NULL};
    RegisterClassExW(&wt);
    hMain=CreateWindowExW(0,L"SnipTr",L"",0,0,0,0,0,HWND_MESSAGE,NULL,hInst,NULL);
    nid.cbSize=sizeof(NOTIFYICONDATAW);nid.hWnd=hMain;nid.uID=1;
    nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;nid.uCallbackMessage=WM_TRAYICON;
    nid.hIcon=LoadIcon(NULL,IDI_APPLICATION);wcscpy_s(nid.szTip,L"SnipTool [Ctrl+Q]");
    Shell_NotifyIconW(NIM_ADD,&nid);
    RegisterHotKey(hMain,HOTKEY_ID,HOTKEY_MOD,HOTKEY_KEY);
    nid.uFlags=NIF_INFO;nid.dwInfoFlags=NIIF_INFO;
    wcscpy_s(nid.szInfoTitle,L"SnipTool");wcscpy_s(nid.szInfo,L"Ctrl+Q — скриншот");
    Shell_NotifyIconW(NIM_MODIFY,&nid);
    SetTimer(hMain,1,30000,ReHot);
    MSG m;
    while(GetMessage(&m,NULL,0,0)){TranslateMessage(&m);DispatchMessage(&m);}
    UnregisterHotKey(hMain,HOTKEY_ID);KillTimer(hMain,1);
    if(hScr)DeleteObject(hScr);GdiplusShutdown(tok);return 0;
}