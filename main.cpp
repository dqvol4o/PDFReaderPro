#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <algorithm>
#include "fpdfview.h"

static FPDF_DOCUMENT doc = nullptr;
static int pageIndex = 0;
static double zoom = 1.0;
static std::wstring fileName;
static HWND mainWindow = nullptr;
static constexpr int ID_OPEN=100, ID_PREV=101, ID_NEXT=102, ID_IN=103, ID_OUT=104;

void UpdateTitle() {
    std::wstring title = L"PDF Reader Pro";
    if(doc) title += L" - " + fileName + L"  [" + std::to_wstring(pageIndex+1) + L"/" + std::to_wstring(FPDF_GetPageCount(doc)) + L"]";
    SetWindowTextW(mainWindow, title.c_str());
}
bool LoadPdfPath(const wchar_t* path) {
    if (!path || !*path) return false;
    int bytes=WideCharToMultiByte(CP_UTF8,0,path,-1,nullptr,0,nullptr,nullptr);
    if(bytes<=0) return false;
    std::string utf8(bytes,'\0');
    WideCharToMultiByte(CP_UTF8,0,path,-1,utf8.data(),bytes,nullptr,nullptr);
    FPDF_DOCUMENT next=FPDF_LoadDocument(utf8.c_str(),nullptr);
    if(!next) {MessageBoxW(mainWindow,L"Неуспешно отваряне на PDF файла.",L"PDF Reader Pro",MB_ICONERROR);return false;}
    if(doc) FPDF_CloseDocument(doc);
    doc=next;pageIndex=0;zoom=1.0;fileName=path;
    UpdateTitle();InvalidateRect(mainWindow,nullptr,TRUE);return true;
}
void OpenDocument() {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner=mainWindow; ofn.lpstrFile=path; ofn.nMaxFile=MAX_PATH;
    ofn.lpstrFilter=L"PDF files\0*.pdf\0All files\0*.*\0";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(!GetOpenFileNameW(&ofn)) return;
    LoadPdfPath(path);
}
void RenderPage(HDC dc, RECT client) {
    RECT area=client; area.top+=44;
    HBRUSH bg=CreateSolidBrush(RGB(88,88,88)); FillRect(dc,&area,bg); DeleteObject(bg);
    if(!doc || FPDF_GetPageCount(doc)==0) {
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
        DrawTextW(dc,L"Ctrl+O — Отвори PDF",-1,&area,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        return;
    }
    FPDF_PAGE page=FPDF_LoadPage(doc,pageIndex); if(!page) return;
    double w=FPDF_GetPageWidth(page),h=FPDF_GetPageHeight(page);
    if(w<=0||h<=0) {FPDF_ClosePage(page);return;}
    double fit=std::min((double)std::max(1,area.right-40)/w,(double)std::max(1,area.bottom-area.top-40)/h);
    int pw=std::max(1,(int)(w*fit*zoom)),ph=std::max(1,(int)(h*fit*zoom));
    // Render at viewport size only: bounds memory usage for oversized pages.
    int rw=std::min(pw,std::max(1,area.right-20));
    int rh=std::min(ph,std::max(1,area.bottom-area.top-20));
    BITMAPINFO bmi={}; bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=rw; bmi.bmiHeader.biHeight=-rh;
    bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32; bmi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP dib=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(dib && bits) {
        FPDF_BITMAP bitmap=FPDFBitmap_CreateEx(rw,rh,FPDFBitmap_BGRx,bits,rw*4);
        if(bitmap) {
            FPDFBitmap_FillRect(bitmap,0,0,rw,rh,0xFFFFFFFF);
            FPDF_RenderPageBitmap(bitmap,page,0,0,pw,ph,0,FPDF_ANNOT);
            HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,dib);
            int x=std::max(10,(area.right-rw)/2), y=area.top+std::max(10,(area.bottom-area.top-rh)/2);
            BitBlt(dc,x,y,rw,rh,mem,0,0,SRCCOPY);
            SelectObject(mem,old);DeleteDC(mem);
            FPDFBitmap_Destroy(bitmap);
        }
        DeleteObject(dib);
    }
    FPDF_ClosePage(page);
}
LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        const wchar_t* labels[]={L"Отвори",L"◀",L"▶",L"+",L"−"};
        int ids[]={ID_OPEN,ID_PREV,ID_NEXT,ID_IN,ID_OUT};
        int x=8;
        for(int i=0;i<5;i++) {int width=i==0?100:50;CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x,7,width,30,hwnd,(HMENU)(INT_PTR)ids[i],GetModuleHandleW(nullptr),nullptr);x+=width+5;}
        return 0;
    }
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case ID_OPEN:OpenDocument();break;
        case ID_PREV:if(doc&&pageIndex>0) --pageIndex;break;
        case ID_NEXT:if(doc&&pageIndex+1<FPDF_GetPageCount(doc)) ++pageIndex;break;
        case ID_IN:zoom=std::min(zoom*1.25,5.0);break;
        case ID_OUT:zoom=std::max(zoom/1.25,0.25);break;
        }
        UpdateTitle();InvalidateRect(hwnd,nullptr,TRUE);return 0;
    case WM_KEYDOWN:
        if(wp==VK_RIGHT||wp==VK_NEXT) SendMessageW(hwnd,WM_COMMAND,ID_NEXT,0);
        if(wp==VK_LEFT||wp==VK_PRIOR) SendMessageW(hwnd,WM_COMMAND,ID_PREV,0);
        return 0;
    case WM_PAINT: {PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);RenderPage(dc,r);EndPaint(hwnd,&ps);return 0;}
    case WM_DESTROY:if(doc)FPDF_CloseDocument(doc);FPDF_DestroyLibrary();PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    FPDF_InitLibrary();
    WNDCLASSW wc={};wc.lpfnWndProc=WndProc;wc.hInstance=instance;wc.lpszClassName=L"PDFReaderProWindow";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    RegisterClassW(&wc);
    mainWindow=CreateWindowW(wc.lpszClassName,L"PDF Reader Pro",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1000,760,nullptr,nullptr,instance,nullptr);
    if(!mainWindow){FPDF_DestroyLibrary();return 1;}
    ShowWindow(mainWindow,show);UpdateWindow(mainWindow);
    // A path supplied by Windows when a PDF is opened with this application.
    int argc=0;
    LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv) {if(argc>1) LoadPdfPath(argv[1]);LocalFree(argv);}
    ACCEL acc={FVIRTKEY|FCONTROL,'O',ID_OPEN};HACCEL accelerator=CreateAcceleratorTableW(&acc,1);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!TranslateAcceleratorW(mainWindow,accelerator,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    DestroyAcceleratorTable(accelerator);return (int)msg.wParam;
}
