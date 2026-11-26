#include "spellcross/inventory.hpp"
#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {
const wxColour bg(24,32,25), panel(36,47,35), fg(225,219,192), muted(167,184,153);
wxString w(const std::string& s) {
    auto text=wxString::FromUTF8(s.data(),s.size());
    return !s.empty() && text.empty()?wxString::From8BitData(s.data(),s.size()):text;
}
std::filesystem::path path(const wxString& s) {
    auto b=s.utf8_str();
    return std::filesystem::u8path(std::string(b.data(),b.length()));
}
void paint(wxWindow* p, bool dark=true) {
    p->SetBackgroundColour(dark?bg:panel);
    p->SetForegroundColour(fg);
}
wxString number(std::uint64_t n) {return wxString::Format("%llu",static_cast<unsigned long long>(n));}
enum {open_dir=wxID_HIGHEST+1,open_file,scan_id,export_one,export_all,save_manifest};
class Frame final : public wxFrame {
    sc::Inventory inv;
    wxString source;
    wxNotebook* tabs{};
    wxStaticText *source_label{},*stats{},*selected_label{},*validation{};
    wxTextCtrl *filter{},*preview_box{},*log_box{};
    wxListCtrl* list{};
    wxButton *export_button{},*all_button{},*scan_button{};
    wxPanel* page(const wxString& label,wxBoxSizer*& s) {
        auto* p=new wxPanel(tabs);
        paint(p);
        s=new wxBoxSizer(wxVERTICAL);p->SetSizer(s);tabs->AddPage(p,label);
        return p;
    }
    wxStaticText* label(wxWindow* p,wxSizer* s,const wxString& text,bool heading=false) {
        auto* t=new wxStaticText(p,wxID_ANY,text);
        t->SetForegroundColour(heading?fg:muted);
        if(heading){auto f=t->GetFont();f.SetPointSize(f.GetPointSize()+4);f.SetWeight(wxFONTWEIGHT_BOLD);t->SetFont(f);}
        s->Add(t,0,wxALL|wxEXPAND,10);
        return t;
    }
    wxButton* button(wxWindow* p,wxSizer* s,int id,const wxString& text) {
        auto* b=new wxButton(p,id,text);s->Add(b,0,wxALL,5);return b;
    }
    void log(const wxString& s) {log_box->AppendText(s+"\n");}
    void choose(bool directory) {
        if(directory) {
            wxDirDialog d(this,"Choose original game directory or mounted disc");
            if(d.ShowModal()!=wxID_OK)return;
            source=d.GetPath();
        } else {
            wxFileDialog d(this,"Choose original disc image or FS archive","","",
                "Disc image or FS archive (*.iso;*.fs)|*.iso;*.fs|All files (*.*)|*.*",wxFD_OPEN|wxFD_FILE_MUST_EXIST);
            if(d.ShowModal()!=wxID_OK)return;
            source=d.GetPath();
        }
        source_label->SetLabel(source);
        analyze();
    }
    void analyze() {
        if(source.empty())return;
        wxBusyCursor busy;
        SetStatusText("Scanning original data...");
        try {
            auto result=sc::scan(path(source));
            inv=std::move(result);
            log("Scanned "+source);
            for(auto& warning:inv.warnings) log("WARNING: "+w(warning));
            stats->SetLabel("Source: "+w(inv.source_kind)+"\n\nInventory records: "+number(inv.assets.size())+
                "\nFS archives parsed: "+number(inv.fs_archives)+"\nWarnings: "+number(inv.warnings.size())+
                "\n\nEdition detection: not yet implemented.");
            validation->SetLabel("FS directory and member bounds checked during scan.\n"+
                number(inv.warnings.size())+" warning(s). See Log for details.\n"+
                "Checksums and decoded-content validation are not yet implemented.");
            refresh();
            SetStatusText("Inventory ready");
            tabs->SetSelection(1);
        } catch(const std::exception& e) {
            inv={};refresh();
            stats->SetLabel("Scan failed. No active inventory.");
            validation->SetLabel("No inventory.");
            log("ERROR: "+w(e.what()));
            SetStatusText("Scan failed");
            wxMessageBox(w(e.what()),"Cannot scan source",wxOK|wxICON_ERROR,this);
        }
        GetSizer()->Layout();
    }
    void refresh() {
        list->Freeze();list->DeleteAllItems();
        const auto needle=filter->GetValue().Lower();
        for(std::size_t i=0;i<inv.assets.size();++i) {
            const auto& a=inv.assets[i];auto name=w(a.name);
            if(!needle.empty()&&!name.Lower().Contains(needle)&&!w(a.format).Lower().Contains(needle))continue;
            auto row=list->InsertItem(list->GetItemCount(),name);
            list->SetItem(row,1,w(a.format));list->SetItem(row,2,number(a.size));
            list->SetItem(row,3,a.member?"Stored member":"Source file");
            list->SetItemData(row,static_cast<wxUIntPtr>(i));
        }
        list->Thaw();
        selected_label->SetLabel("Select a record for a stored-byte preview.");
        preview_box->Clear();
        export_button->Enable(false);
        all_button->Enable(std::any_of(inv.assets.begin(),inv.assets.end(),[](const auto& a){return a.member;}));
    }
    const sc::Asset* selected() {
        auto row=list->GetNextItem(-1,wxLIST_NEXT_ALL,wxLIST_STATE_SELECTED);
        if(row<0)return nullptr;
        auto idx=static_cast<std::size_t>(list->GetItemData(row));
        return idx<inv.assets.size()?&inv.assets[idx]:nullptr;
    }
    void show_preview() {
        auto* a=selected();export_button->Enable(a!=nullptr);
        if(!a){preview_box->Clear();selected_label->SetLabel("Select a record.");return;}
        selected_label->SetLabel(w(a->name)+"  |  "+number(a->size)+" bytes");
        try {
            auto data=sc::preview(*a);
            std::ostringstream hex;hex<<std::hex<<std::setfill('0');
            for(std::size_t i=0;i<data.size();i+=16) {
                hex<<std::setw(8)<<i<<"  ";
                for(std::size_t j=0;j<16;++j)
                    if(i+j<data.size())hex<<std::setw(2)<<unsigned(data[i+j])<<' ';else hex<<"   ";
                hex<<" |";
                for(std::size_t j=i;j<std::min(i+16,data.size());++j)
                    hex<<char(data[j]>=32&&data[j]<127?data[j]:'.');
                hex<<"|\n";
            }
            preview_box->SetValue(w(hex.str()));
        }catch(const std::exception& e){preview_box->SetValue(w(e.what()));log("ERROR: "+w(e.what()));}
    }
    void export_assets(bool all) {
        std::vector<sc::Asset> entries;
        if(all){for(auto& a:inv.assets)if(a.member)entries.push_back(a);}
        else if(auto* a=selected())entries.push_back(*a);
        if(entries.empty())return;
        wxDirDialog d(this,"Choose output folder (a new export subfolder will be created)");
        if(d.ShowModal()!=wxID_OK)return;
        wxBusyCursor busy;
        try {
            auto dest=sc::extract(entries,path(d.GetPath()));
            log("Extracted "+number(entries.size())+" record(s) to "+w(sc::utf8(dest)));
            wxMessageBox("Raw bytes exported to:\n"+w(sc::utf8(dest)),"Extraction complete",wxOK|wxICON_INFORMATION,this);
        }catch(const std::exception& e){log("ERROR: "+w(e.what()));wxMessageBox(w(e.what()),"Extraction failed",wxOK|wxICON_ERROR,this);}
    }
    void export_manifest() {
        if(inv.assets.empty())return;
        wxDirDialog d(this,"Choose directory for new manifest folder");
        if(d.ShowModal()!=wxID_OK)return;
        try {
            auto parent=path(d.GetPath());std::filesystem::path dest;
            for(unsigned i=1;i<=10000;++i){
                auto p=parent/("Spellcross-inventory-"+std::to_string(i));
                if(std::filesystem::create_directory(p)){dest=p;break;}
            }
            if(dest.empty())throw std::runtime_error("Cannot create manifest folder");
            std::ofstream f(dest/"inventory.tsv",std::ios::binary);
            f<<sc::manifest(inv);f.close();
            if(!f)throw std::runtime_error("Manifest write failed");
            log("Manifest saved: "+w(sc::utf8(dest)));
            SetStatusText("Manifest saved");
        }catch(const std::exception& e){log("ERROR: "+w(e.what()));wxMessageBox(w(e.what()),"Export failed",wxOK|wxICON_ERROR,this);}
    }
public:
    Frame():wxFrame(nullptr,wxID_ANY,"Spellcross Asset Converter",wxDefaultPosition,wxSize(1180,800)) {
        SetMinSize(wxSize(800,600));paint(this);
        auto* menu=new wxMenu;
        menu->Append(open_dir,"Open game &directory...");
        menu->Append(open_file,"Open &ISO / FS...");
        menu->Append(scan_id,"&Rescan");
        menu->Append(save_manifest,"Save inventory &manifest...");
        menu->AppendSeparator();menu->Append(wxID_EXIT,"E&xit");
        auto* help=new wxMenu;help->Append(wxID_ABOUT,"&About");
        auto* bar=new wxMenuBar;bar->Append(menu,"&File");bar->Append(help,"&Help");SetMenuBar(bar);
        auto* root=new wxBoxSizer(wxVERTICAL);SetSizer(root);
        auto* header=new wxPanel(this);paint(header,false);auto* hs=new wxBoxSizer(wxVERTICAL);header->SetSizer(hs);
        label(header,hs,"SPELLCROSS  /  ASSET CONVERTER",true);
        label(header,hs,"RELOADED TOOLKIT   |   v0.1.0   |   Original data inventory");
        root->Add(header,0,wxEXPAND);
        tabs=new wxNotebook(this,wxID_ANY);root->Add(tabs,1,wxALL|wxEXPAND,10);
        wxBoxSizer* s;
        auto* overview=page("Overview",s);
        label(overview,s,"Original game source",true);
        source_label=label(overview,s,"No source selected");
        auto* buttons=new wxBoxSizer(wxHORIZONTAL);s->Add(buttons,0,wxALL,5);
        button(overview,buttons,open_dir,"Open directory");
        button(overview,buttons,open_file,"Open ISO / FS");
        scan_button=button(overview,buttons,scan_id,"Rescan");
        stats=label(overview,s,"Open original game data to build an inventory.");
        auto* browser=page("Asset Browser",s);
        label(browser,s,"Asset inventory",true);
        filter=new wxTextCtrl(browser,wxID_ANY);filter->SetHint("Filter by filename or extension");s->Add(filter,0,wxEXPAND|wxLEFT|wxRIGHT,10);
        list=new wxListCtrl(browser,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxLC_REPORT|wxLC_SINGLE_SEL);
        paint(list,false);list->InsertColumn(0,"Logical path",wxLIST_FORMAT_LEFT,440);
        list->InsertColumn(1,"Format",wxLIST_FORMAT_LEFT,100);
        list->InsertColumn(2,"Bytes",wxLIST_FORMAT_RIGHT,130);
        list->InsertColumn(3,"Storage",wxLIST_FORMAT_LEFT,160);
        s->Add(list,1,wxEXPAND|wxALL,10);
        auto* bottom=new wxBoxSizer(wxHORIZONTAL);s->Add(bottom,0,wxEXPAND|wxALL,5);
        export_button=button(browser,bottom,export_one,"Extract selected");export_button->Enable(false);
        button(browser,bottom,save_manifest,"Save inventory manifest");
        selected_label=label(browser,s,"Select a record for a stored-byte preview.");
        preview_box=new wxTextCtrl(browser,wxID_ANY,"",wxDefaultPosition,wxSize(-1,180),wxTE_MULTILINE|wxTE_READONLY|wxTE_DONTWRAP);
        paint(preview_box,false);preview_box->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
        s->Add(preview_box,0,wxEXPAND|wxALL,10);
        auto* conversion=page("Extraction / Conversion",s);
        label(conversion,s,"Lossless raw extraction",true);
        label(conversion,s,"Export all parsed FS members to a fresh folder.\nStored bytes are preserved; LZ decoding and modern format conversion are future milestones.");
        all_button=button(conversion,s,export_all,"Extract all FS members");all_button->Enable(false);
        auto* valid=page("Validation",s);label(valid,s,"Structural checks",true);
        validation=label(valid,s,"No inventory.");
        auto* logs=page("Log",s);label(logs,s,"Operation log",true);
        log_box=new wxTextCtrl(logs,wxID_ANY,"",wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE|wxTE_READONLY);
        paint(log_box,false);s->Add(log_box,1,wxALL|wxEXPAND,10);
        CreateStatusBar();SetStatusText("Ready - select original Spellcross data");
        Bind(wxEVT_MENU,[this](wxCommandEvent&){choose(true);},open_dir);
        Bind(wxEVT_MENU,[this](wxCommandEvent&){choose(false);},open_file);
        Bind(wxEVT_MENU,[this](wxCommandEvent&){analyze();},scan_id);
        Bind(wxEVT_MENU,[this](wxCommandEvent&){export_manifest();},save_manifest);
        Bind(wxEVT_MENU,[this](wxCommandEvent&){Close();},wxID_EXIT);
        Bind(wxEVT_MENU,[this](wxCommandEvent&){wxMessageBox("Spellcross Asset Converter 0.1.0\nPart of Spellcross Reloaded\nMIT license\nFS format based on Stanislav Maslan's restoration tools.","About",wxOK|wxICON_INFORMATION,this);},wxID_ABOUT);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){choose(true);},open_dir);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){choose(false);},open_file);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){analyze();},scan_id);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_manifest();},save_manifest);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_assets(false);},export_one);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_assets(true);},export_all);
        filter->Bind(wxEVT_TEXT,[this](wxCommandEvent&){refresh();});
        list->Bind(wxEVT_LIST_ITEM_SELECTED,[this](wxListEvent&){show_preview();});
        list->Bind(wxEVT_LIST_ITEM_DESELECTED,[this](wxListEvent&){show_preview();});
        Centre();
    }
};
class App final:public wxApp {
public:
    bool OnInit() override {auto* frame=new Frame;frame->Show();return true;}
};
}
wxIMPLEMENT_APP(App);
