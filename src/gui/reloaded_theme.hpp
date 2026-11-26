#pragma once
// Visual vocabulary and native owner-drawn button approach adapted from
// Spellcross Reloaded game_menu_bar.h / strategic_ui_theme.h.
// No dependency on or modification to the game executable.
#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/simplebook.h>
#include <wx/fontenum.h>
#include <functional>
#include <vector>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <commctrl.h>
#endif
namespace sc::ui {
inline wxColour Background(){return {8,24,7};}
inline wxColour Highlight(){return {24,51,15};}
inline wxColour Accent(){return {61,207,32};}
inline wxColour Text(){return {62,224,25};}
inline wxColour Heading(){return {227,233,24};}
inline wxColour Inactive(){return {115,120,124};}
inline wxFont Font(int size=11){
    if(wxFontEnumerator::IsValidFacename("Fixedsys Excelsior 3.01"))
        return wxFont(wxFontInfo(size).FaceName("Fixedsys Excelsior 3.01"));
    return wxFont(wxFontInfo(size).Family(wxFONTFAMILY_TELETYPE));
}
inline void Grid(wxDC& dc,wxWindow* window,bool highlighted=false){
    dc.SetBackground(wxBrush(highlighted?Highlight():Background()));dc.Clear();
    dc.SetPen(wxPen(wxColour(19,43,14)));
    const int step=window->FromDIP(12),start=window->FromDIP(8);
    auto size=window->GetClientSize();
    for(int x=start;x<size.x;x+=step)dc.DrawLine(x,1,x,size.y-1);
    for(int y=start;y<size.y;y+=step)dc.DrawLine(1,y,size.x-1,y);
}
class Panel final:public wxPanel {
public:
    explicit Panel(wxWindow* parent):wxPanel(parent){
        SetBackgroundColour(Background());SetForegroundColour(Text());
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT,[this](wxPaintEvent&){wxAutoBufferedPaintDC dc(this);Grid(dc,this);});
    }
};
class Button final:public wxButton {
    bool selected=false;
    void Paint(wxDC& dc,bool pressed,bool focused){
        bool active=IsEnabled()&&(selected||pressed||GetClientRect().Contains(ScreenToClient(wxGetMousePosition())));
        Grid(dc,this,active);
        dc.SetPen(wxPen(IsEnabled()?Accent():Inactive()));dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(GetClientRect());
        if(focused&&IsEnabled()){
            dc.SetPen(wxPen(Heading(),1,wxPENSTYLE_DOT));
            dc.DrawRectangle(GetClientRect().Deflate(FromDIP(3)));
        }
        dc.SetFont(GetFont());
        dc.SetTextForeground(!IsEnabled()?Inactive():active?Heading():Text());
        auto rect=GetClientRect().Deflate(FromDIP(5),FromDIP(3));
        if(pressed)rect.Offset(1,1);
        dc.SetClippingRegion(rect);dc.DrawLabel(wxControl::GetLabelText(GetLabel()),rect,wxALIGN_CENTER);dc.DestroyClippingRegion();
    }
public:
    Button(wxWindow* parent,wxWindowID id,const wxString& text):wxButton(parent,id,text){
        SetFont(Font());SetBackgroundColour(Background());SetForegroundColour(Text());
        auto best=GetBestSize();
        SetMinSize(wxSize((std::max)(FromDIP(36),best.x+FromDIP(10)),(std::max)(FromDIP(34),best.y)));
#ifdef __WXMSW__
        MakeOwnerDrawn();
#else
        Bind(wxEVT_PAINT,[this](wxPaintEvent&){wxPaintDC dc(this);Paint(dc,HasCapture()&&wxGetMouseState().LeftIsDown(),HasFocus());});
#endif
        Bind(wxEVT_ENTER_WINDOW,[this](wxMouseEvent& e){Refresh();e.Skip();});
        Bind(wxEVT_LEAVE_WINDOW,[this](wxMouseEvent& e){Refresh();e.Skip();});
        Bind(wxEVT_SET_FOCUS,[this](wxFocusEvent& e){Refresh();e.Skip();});
        Bind(wxEVT_KILL_FOCUS,[this](wxFocusEvent& e){Refresh();e.Skip();});
    }
    void Select(bool v){selected=v;Refresh();}
#ifdef __WXMSW__
    bool MSWOnDraw(WXDRAWITEMSTRUCT* item) override {
        auto* draw=reinterpret_cast<DRAWITEMSTRUCT*>(item);auto size=GetClientSize();
        if(size.x<=0||size.y<=0)return true;
        wxBitmap image(size.x,size.y);wxMemoryDC dc(image);
        Paint(dc,(draw->itemState&ODS_SELECTED)!=0,(draw->itemState&ODS_FOCUS)!=0);
        BitBlt(draw->hDC,0,0,size.x,size.y,reinterpret_cast<HDC>(dc.GetHDC()),0,0,SRCCOPY);
        return true;
    }
#endif
};
// Use real wx buttons for keyboard navigation and native notifications,
// rather than native notebook tab chrome that clips or ignores colors.
class Book final:public wxSimplebook {
    wxWindow* nav_parent;
    wxSizer* nav;
    std::vector<Button*> selectors;
    void Sync(){
        for(std::size_t i=0;i<selectors.size();++i)selectors[i]->Select(int(i)==GetSelection());
    }
public:
    std::function<void(int)> changed;
    Book(wxWindow* parent,wxWindow* buttons,wxSizer* layout)
        :wxSimplebook(parent,wxID_ANY),nav_parent(buttons),nav(layout){
        SetBackgroundColour(Background());SetForegroundColour(Text());
    }
    bool AddPage(wxWindow* page,const wxString& title,bool select=false,int imageId=wxNOT_FOUND) override{
        if(!wxSimplebook::AddPage(page,title,select,imageId))return false;
        auto index=selectors.size();auto* b=new Button(nav_parent,wxID_ANY,title);
        selectors.push_back(b);nav->Add(b,0,wxEXPAND|wxALL,2);
        b->Bind(wxEVT_BUTTON,[this,index](wxCommandEvent&){SetSelection(index);});
        Sync();return true;
    }
    int SetSelection(std::size_t n) override{
        auto old=wxSimplebook::SetSelection(n);Sync();
        if(changed&&old!=GetSelection())changed(GetSelection());
        return old;
    }
};
}
