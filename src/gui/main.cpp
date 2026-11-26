#include "spellcross/inventory.hpp"
#include "spellcross/graphics.hpp"
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/scrolwin.h>
#include <wx/dcbuffer.h>
#include <wx/progdlg.h>
#include <wx/timer.h>
#include <wx/image.h>
#include <map>
#include <optional>
#include <limits>
#include <cmath>
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
enum {open_dir=wxID_HIGHEST+1,open_file,scan_id,export_one,export_all,save_manifest,
    prev_id,next_id,play_id,zoom_in,zoom_out,zoom_fit,clear_id,png_one,png_group,validate_id,decoded_one};
class ItemData final:public wxTreeItemData {
public:
    std::size_t index;
    explicit ItemData(std::size_t i):index(i){}
};
class Canvas final:public wxScrolledWindow {
    wxImage image;
    wxBitmap bitmap;
    int zoom=0; // 0 = fit
    void rebuild(){
        bitmap=wxNullBitmap;
        if(image.IsOk()){
            auto size=GetClientSize();
            double scale=zoom?zoom:std::max(0.01,std::min(4.0,std::min(
                double(std::max(1,size.x-24))/image.GetWidth(),
                double(std::max(1,size.y-24))/image.GetHeight())));
            int width=std::max(1,int(image.GetWidth()*scale)),height=std::max(1,int(image.GetHeight()*scale));
            // Avoid huge bitmap allocations from malformed/large assets.
            if(std::uint64_t(width)*height>16000000){
                double limit=std::sqrt(16000000.0/(double(width)*height));
                width=std::max(1,int(width*limit));height=std::max(1,int(height*limit));
            }
            bitmap=wxBitmap(image.Scale(width,height,wxIMAGE_QUALITY_NEAREST));
            SetVirtualSize(width+24,height+24);
        }else SetVirtualSize(1,1);
        Refresh();
    }
public:
    explicit Canvas(wxWindow* parent):wxScrolledWindow(parent,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxHSCROLL|wxVSCROLL|wxBORDER_SIMPLE){
        SetBackgroundStyle(wxBG_STYLE_PAINT);SetScrollRate(16,16);
        Bind(wxEVT_PAINT,[this](wxPaintEvent&){
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(panel));dc.Clear();PrepareDC(dc);
            if(!bitmap.IsOk()){
                dc.SetTextForeground(muted);dc.DrawText("Select a sprite or palette.",12,12);return;
            }
            auto size=GetClientSize();
            int x=std::max(12,(size.x-bitmap.GetWidth())/2),y=std::max(12,(size.y-bitmap.GetHeight())/2);
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(wxColour(72,79,69)));dc.DrawRectangle(x,y,bitmap.GetWidth(),bitmap.GetHeight());
            dc.DrawBitmap(bitmap,x,y,true);
        });
        Bind(wxEVT_SIZE,[this](wxSizeEvent& e){e.Skip();if(!zoom)rebuild();});
    }
    void SetImage(const wxImage& i){image=i;Scroll(0,0);rebuild();}
    void SetZoom(int delta){zoom=delta==0?0:std::clamp((zoom?zoom:1)+delta,1,8);Scroll(0,0);rebuild();}
};
wxImage sprite_image(const sc::Sprite& sprite,const sc::Palette& palette,int mode=0){
    if(sprite.width<=0||sprite.height<=0)throw std::runtime_error("Empty sprite");
    wxImage image(sprite.width,sprite.height);
    if(mode==0)image.InitAlpha();
    auto* rgb=image.GetData();auto* alpha=mode==0?image.GetAlpha():nullptr;
    for(std::size_t i=0;i<sprite.indices.size();++i){
        auto idx=sprite.indices[i];
        for(int ch=0;ch<3;++ch)rgb[i*3+ch]=mode==1?idx:mode==2?sprite.opaque[i]:idx==253?0:palette.rgb[idx*3+ch];
        if(alpha)alpha[i]=!sprite.opaque[i]?0:idx==253?128:255;
    }
    return image;
}
std::filesystem::path fresh_dir(const std::filesystem::path& parent,const std::string& prefix){
    for(unsigned i=1;i<=10000;++i){
        auto p=parent/(prefix+std::to_string(i));
        if(std::filesystem::create_directory(p))return p;
    }
    throw std::runtime_error("Cannot allocate output folder");
}
void write_bytes(const std::filesystem::path& p,const sc::Bytes& data){
    std::ofstream out(p,std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    out.close();if(!out)throw std::runtime_error("Output write failed");
}
void save_sprite(const sc::Asset& a,const sc::Palette& p,const std::filesystem::path& dest){
    auto s=sc::decode_sprite(sc::decode_asset(a));
    if(!std::filesystem::create_directory(dest))throw std::runtime_error("Sprite directory already exists");
    const char* names[]={"preview.png","indices.png","opacity.png"};
    for(int i=0;i<3;++i)
        if(!sprite_image(s,p,i).SaveFile(w(sc::utf8(dest/names[i])),wxBITMAP_TYPE_PNG))
            throw std::runtime_error("PNG export failed");
    write_bytes(dest/"original.bin",sc::preview(a,static_cast<std::size_t>(a.size)));
    std::ofstream meta(dest/"sprite.json",std::ios::binary);
    meta<<sc::sprite_metadata(a,s,p);meta.close();if(!meta)throw std::runtime_error("Metadata write failed");
}
class Frame final : public wxFrame {
    sc::Inventory inv;
    wxString source;
    wxNotebook* tabs{};
    wxStaticText *source_label{},*stats{},*selected_label{},*validation{};
    wxTextCtrl *filter{},*preview_box{},*log_box{};
    wxTreeCtrl* tree{};
    wxNotebook* preview_tabs{};
    Canvas* graphic{};
    wxTextCtrl *metadata{},*raw_hex{};
    wxButton *prev_button{},*next_button{},*play_button{},*png_button{},*group_button{},*decoded_button{};
    wxTimer timer;
    std::vector<wxTreeItemId> nodes;
    std::optional<std::size_t> active;
    std::vector<std::size_t> sequence;
    std::size_t sequence_pos{};
    bool rebuilding=false;
    wxChoice* kind{};
    wxStaticText* count_label{};
    sc::Palette palette;
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
        stop_play();
        if(directory) {
            wxDirDialog d(this,"Choose original game directory or mounted disc");
            if(d.ShowModal()!=wxID_OK)return;
            source=d.GetPath();
        } else {
            wxFileDialog d(this,"Choose original disc image or FS archive","","",
                "Disc image or FS / FSU archive (*.iso;*.fs;*.fsu)|*.iso;*.fs;*.fsu|All files (*.*)|*.*",wxFD_OPEN|wxFD_FILE_MUST_EXIST);
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
            stop_play();active.reset();
            inv=std::move(result);
            palette=sc::tactical_palette(inv);
            log("Scanned "+source);
            for(auto& warning:inv.warnings) log("WARNING: "+w(warning));
            stats->SetLabel("Source: "+w(inv.source_kind)+"\n\nInventory records: "+number(inv.assets.size())+
                "\nFS archives parsed: "+number(inv.fs_archives)+"\nFSU archives: "+number(inv.fsu_archives)+
                "\nSprite groups: "+number(inv.fsu_resources)+"\nSprites: "+number(inv.fsu_sprites)+"\nWarnings: "+number(inv.warnings.size())+
                "\n\nEdition detection: not yet implemented.");
            validation->SetLabel("FS / FSU directory and member bounds checked during scan.\n"+
                number(inv.warnings.size())+" warning(s). See Log for details.\n"+
                "Use Validate compressed assets to check LZ and FSU decoding. Checksums are not yet implemented.");
            refresh();
            SetStatusText("Inventory ready");
            tabs->SetSelection(1);
        } catch(const std::exception& e) {
            stop_play();active.reset();inv={};refresh();
            stats->SetLabel("Scan failed. No active inventory.");
            validation->SetLabel("No inventory.");
            log("ERROR: "+w(e.what()));
            SetStatusText("Scan failed");
            wxMessageBox(w(e.what()),"Cannot scan source",wxOK|wxICON_ERROR,this);
        }
        GetSizer()->Layout();
    }
    void stop_play(){timer.Stop();if(play_button)play_button->SetLabel("Play sequence");}
    void refresh() {
        stop_play();rebuilding=true;
        const auto old=active;
        tree->Freeze();tree->DeleteAllItems();
        nodes.assign(inv.assets.size(),wxTreeItemId{});
        auto root=tree->AddRoot("Original game data");
        std::map<std::string,wxTreeItemId> folders;
        folders[""]=root;
        const auto needle=filter->GetValue().Lower();
        auto type=kind->GetStringSelection();
        unsigned visible=0;
        for(std::size_t i=0;i<inv.assets.size();++i){
            const auto& a=inv.assets[i];auto name=w(a.name);
            if(!needle.empty()&&!name.Lower().Contains(needle)&&!w(a.format).Lower().Contains(needle))continue;
            if(type=="Sprites"&&a.format!="FSU-SPRITE")continue;
            if(type=="Palettes"&&a.format!="PAL")continue;
            if(type=="Compressed"&&a.format!="LZ"&&a.format!="LZ0"&&a.format!="FSU-SPRITE")continue;
            if(type=="Archives"&&a.format!="FS"&&a.format!="FSU")continue;
            if(type=="Text / definitions"&&a.format!="DEF"&&a.format!="TXT"&&a.format!="BRF"&&a.format!="TUT")continue;
            if(type=="Maps"&&a.format!="DTA")continue;
            std::string logical=a.name;
            for(std::size_t p=0;(p=logical.find("::",p))!=std::string::npos;)logical.replace(p,2,"/");
            std::string key;auto parent=root;
            for(std::size_t from=0;;){
                auto end=logical.find('/',from);
                auto part=logical.substr(from,end==std::string::npos?end:end-from);
                if(!key.empty())key+='/';key+=part;
                auto it=folders.find(key);
                if(it==folders.end()){
                    parent=tree->AppendItem(parent,w(part));
                    folders.emplace(key,parent);
                }else parent=it->second;
                if(end==std::string::npos){
                    // Retain duplicate records as separate items.
                    if(tree->GetItemData(parent))parent=tree->AppendItem(tree->GetItemParent(parent),w(part+" [duplicate]"));
                    tree->SetItemData(parent,new ItemData(i));nodes[i]=parent;++visible;break;
                }
                from=end+1;
            }
        }
        tree->Expand(root);
        if(!needle.empty()||type!="All assets"){
            // Expand matching branches, without expanding an unfiltered full inventory.
            for(auto& [key,item]:folders)if(tree->ItemHasChildren(item))tree->Expand(item);
        }
        if(old&&*old<nodes.size()&&nodes[*old].IsOk()){tree->SelectItem(nodes[*old]);tree->EnsureVisible(nodes[*old]);}
        tree->Thaw();rebuilding=false;
        count_label->SetLabel(number(visible)+" matching records");
        all_button->Enable(std::any_of(inv.assets.begin(),inv.assets.end(),[](const auto& a){return a.member;}));
        show_preview();
    }
    const sc::Asset* selected(){
        auto item=tree->GetSelection();
        if(!item.IsOk())return nullptr;
        auto* data=dynamic_cast<ItemData*>(tree->GetItemData(item));
        return data&&data->index<inv.assets.size()?&inv.assets[data->index]:nullptr;
    }
    wxString hex(const sc::Bytes& data){
        std::ostringstream o;o<<std::hex<<std::setfill('0');
        for(std::size_t i=0;i<std::min<std::size_t>(4096,data.size());i+=16){
            o<<std::setw(8)<<i<<"  ";
            for(std::size_t j=0;j<16;++j)
                if(i+j<data.size())o<<std::setw(2)<<unsigned(data[i+j])<<' ';else o<<"   ";
            o<<" |";
            for(std::size_t j=i;j<std::min(i+16,data.size());++j)o<<char(data[j]>=32&&data[j]<127?data[j]:'.');
            o<<"|\n";
        }return w(o.str());
    }
    void show_preview(){
        if(rebuilding)return;
        auto* a=selected();
        active=a?std::optional<std::size_t>(a-inv.assets.data()):std::nullopt;
        export_button->Enable(a!=nullptr);
        png_button->Enable(false);group_button->Enable(false);decoded_button->Enable(false);
        graphic->SetImage(wxImage());preview_box->Clear();raw_hex->Clear();metadata->Clear();
        sequence.clear();sequence_pos=0;
        if(!a){stop_play();selected_label->SetLabel("Select an asset.");prev_button->Enable(false);next_button->Enable(false);play_button->Enable(false);return;}
        sequence=sc::sprite_group_members(inv,*a);
        sequence.erase(std::remove_if(sequence.begin(),sequence.end(),[this](auto i){return i>=nodes.size()||!nodes[i].IsOk();}),sequence.end());
        auto where=std::find(sequence.begin(),sequence.end(),*active);
        if(where!=sequence.end())sequence_pos=static_cast<std::size_t>(where-sequence.begin());
        prev_button->Enable(sequence.size()>1);next_button->Enable(sequence.size()>1);play_button->Enable(sequence.size()>1);
        if(sequence.size()<2)stop_play();
        selected_label->SetLabel(w(a->name));
        wxString details="Logical path: "+w(a->name)+"\nFormat: "+w(a->format)+"\nStored bytes: "+number(a->size)+"\nOffset: "+number(a->offset)+"\nBacking file: "+w(sc::utf8(a->backing));
        try{
            auto stored=sc::preview(*a);
            raw_hex->SetValue(hex(stored));
            auto data=stored;
            if(a->format=="FSU-SPRITE"||a->format=="LZ"||a->format=="LZ0"){
                data=sc::decode_asset(*a);decoded_button->Enable(true);
                details+="\nDecoded bytes: "+number(data.size());
            }
            preview_box->SetValue(hex(data));
            if(a->format=="FSU-SPRITE"){
                auto s=sc::decode_sprite(data);
                if(s.height>0){
                    graphic->SetImage(sprite_image(s,palette));png_button->Enable(true);group_button->Enable(true);
                }
                details+="\nDimensions: "+number(s.width)+" x "+number(s.height)+
                    "\nOrigin: "+wxString::Format("%d, %d",s.x,s.y)+
                    "\nTrailing bytes: "+number(s.trailing_bytes)+
                    "\nFrame sequence: "+number(sequence_pos+1)+" / "+number(sequence.size())+
                    "\nPalette: "+(std::all_of(palette.known.begin(),palette.known.end(),[](bool v){return v;})?wxString("tactical"):wxString("incomplete; grayscale fallback"))+
                    "\nShadow index 253: approximate display only.";
            }else if(a->format=="PAL"&&a->size>=3&&a->size<=768&&a->size%3==0){
                int n=static_cast<int>(a->size/3);
                wxImage image(256,((n+15)/16)*16);
                image.SetRGB(wxRect(0,0,image.GetWidth(),image.GetHeight()),36,47,35);
                for(int i=0;i<n;++i)image.SetRGB(wxRect((i%16)*16,(i/16)*16,16,16),data[i*3],data[i*3+1],data[i*3+2]);
                graphic->SetImage(image);
            }
        }catch(const std::exception& e){details+="\nERROR: "+w(e.what());log("ERROR: "+w(e.what()));}
        metadata->SetValue(details);
    }
    void step(int direction){
        if(sequence.size()<2)return;
        auto next=(sequence_pos+sequence.size()+(direction>0?1:sequence.size()-1))%sequence.size();
        auto item=nodes[sequence[next]];
        if(item.IsOk()){tree->SelectItem(item);tree->EnsureVisible(item);}
    }
    void export_png(bool group){
        stop_play();auto* a=selected();if(!a||a->format!="FSU-SPRITE")return;
        auto ids=group?sc::sprite_group_members(inv,*a):std::vector<std::size_t>{static_cast<std::size_t>(a-inv.assets.data())};
        wxDirDialog d(this,"Choose folder for sprite PNG bundles");
        if(d.ShowModal()!=wxID_OK)return;
        std::filesystem::path dest;
        try{
            dest=fresh_dir(path(d.GetPath()),"Spellcross-sprites-");
            wxProgressDialog progress("Sprite export","Exporting PNG and original data...",static_cast<int>(ids.size()),this,wxPD_APP_MODAL|wxPD_CAN_ABORT|wxPD_AUTO_HIDE);
            std::size_t done=0,failed=0;bool cancelled=false;
            for(auto id:ids){
                if(!progress.Update(static_cast<int>(done+failed),w(inv.assets[id].name))){cancelled=true;break;}
                auto sub=dest/("sprite-"+std::to_string(id));
                try{save_sprite(inv.assets[id],palette,sub);++done;}
                catch(const std::exception& e){++failed;std::error_code ec;std::filesystem::remove_all(sub,ec);log("ERROR: "+w(inv.assets[id].name)+": "+w(e.what()));}
            }
            progress.Update(static_cast<int>(ids.size()));
            auto message=number(done)+" exported, "+number(failed)+" failed"+(cancelled?wxString(" (cancelled)"):wxString())+"\n"+w(sc::utf8(dest));
            log(message);wxMessageBox(message,"Sprite export",wxOK|wxICON_INFORMATION,this);
        }catch(const std::exception& e){log("ERROR: "+w(e.what()));wxMessageBox(w(e.what()),"Export failed",wxOK|wxICON_ERROR,this);}
    }
    void export_decoded(){
        stop_play();auto* a=selected();if(!a)return;
        wxDirDialog d(this,"Choose folder for decoded data");
        if(d.ShowModal()!=wxID_OK)return;
        try{
            auto data=sc::decode_asset(*a);
            auto dest=fresh_dir(path(d.GetPath()),"Spellcross-decoded-");
            write_bytes(dest/"decoded.bin",data);
            write_bytes(dest/"original.bin",sc::preview(*a,static_cast<std::size_t>(a->size)));
            log("Decoded data exported: "+w(sc::utf8(dest)));
            wxMessageBox(w(sc::utf8(dest)),"Decoded export",wxOK|wxICON_INFORMATION,this);
        }catch(const std::exception& e){log("ERROR: "+w(e.what()));wxMessageBox(w(e.what()),"Export failed",wxOK|wxICON_ERROR,this);}
    }
    void validate_decoding(){
        stop_play();
        std::vector<std::size_t> ids;
        for(std::size_t i=0;i<inv.assets.size();++i)
            if(inv.assets[i].format=="FSU-SPRITE"||inv.assets[i].format=="LZ"||inv.assets[i].format=="LZ0")ids.push_back(i);
        if(ids.empty()){validation->SetLabel("No supported compressed assets in inventory.");return;}
        wxProgressDialog progress("Decode validation","Checking compressed assets...",static_cast<int>(ids.size()),this,wxPD_APP_MODAL|wxPD_CAN_ABORT|wxPD_AUTO_HIDE);
        std::size_t ok=0,bad=0;bool cancelled=false;
        for(auto id:ids){
            const auto& a=inv.assets[id];
            if((ok+bad)%32==0&&!progress.Update(static_cast<int>(ok+bad),w(a.name))){cancelled=true;break;}
            try{auto d=sc::decode_asset(a);if(a.format=="FSU-SPRITE")sc::decode_sprite(d);++ok;}
            catch(const std::exception& e){++bad;log("VALIDATION: "+w(a.name)+": "+w(e.what()));}
        }
        progress.Update(static_cast<int>(ids.size()));
        auto summary="Decode validation: "+number(ok)+" passed, "+number(bad)+" failed, "+number(ids.size()-ok-bad)+" not checked.\n"+
            number(inv.warnings.size())+" structural warnings. See Log."+(cancelled?wxString("\nCancelled."):wxString());
        validation->SetLabel(summary);log(summary);
    }
    void export_assets(bool all) {
        stop_play();
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
        stop_play();
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
    Frame():wxFrame(nullptr,wxID_ANY,"Spellcross Asset Converter",wxDefaultPosition,wxSize(1180,800)),timer(this) {
        SetMinSize(wxSize(960,600));paint(this);
        auto* menu=new wxMenu;
        menu->Append(open_dir,"Open game &directory...");
        menu->Append(open_file,"Open &ISO / FS / FSU...");
        menu->Append(scan_id,"&Rescan");
        menu->Append(save_manifest,"Save inventory &manifest...");
        menu->AppendSeparator();menu->Append(wxID_EXIT,"E&xit");
        auto* help=new wxMenu;help->Append(wxID_ABOUT,"&About");
        auto* bar=new wxMenuBar;bar->Append(menu,"&File");bar->Append(help,"&Help");SetMenuBar(bar);
        auto* root=new wxBoxSizer(wxVERTICAL);SetSizer(root);
        auto* header=new wxPanel(this);paint(header,false);auto* hs=new wxBoxSizer(wxVERTICAL);header->SetSizer(hs);
        label(header,hs,"SPELLCROSS  /  ASSET CONVERTER",true);
        label(header,hs,"RELOADED TOOLKIT   |   v0.3.0   |   Original data inventory");
        root->Add(header,0,wxEXPAND);
        tabs=new wxNotebook(this,wxID_ANY);root->Add(tabs,1,wxALL|wxEXPAND,10);
        wxBoxSizer* s;
        auto* overview=page("Overview",s);
        label(overview,s,"Original game source",true);
        source_label=label(overview,s,"No source selected");
        auto* buttons=new wxBoxSizer(wxHORIZONTAL);s->Add(buttons,0,wxALL,5);
        button(overview,buttons,open_dir,"Open directory");
        button(overview,buttons,open_file,"Open ISO / FS / FSU");
        scan_button=button(overview,buttons,scan_id,"Rescan");
        stats=label(overview,s,"Open original game data to build an inventory.");
        auto* browser=page("Asset Browser",s);
        auto* search=new wxBoxSizer(wxHORIZONTAL);s->Add(search,0,wxEXPAND|wxALL,6);
        filter=new wxTextCtrl(browser,wxID_ANY);filter->SetHint("Search paths, names or formats");
        search->Add(filter,1,wxALL|wxALIGN_CENTER_VERTICAL,4);
        kind=new wxChoice(browser,wxID_ANY);for(auto name:{"All assets","Sprites","Palettes","Compressed","Archives","Text / definitions","Maps"})kind->Append(name);
        kind->SetSelection(0);search->Add(kind,0,wxALL|wxALIGN_CENTER_VERTICAL,4);
        count_label=new wxStaticText(browser,wxID_ANY,"0 matching records");count_label->SetForegroundColour(muted);
        search->Add(count_label,0,wxALL|wxALIGN_CENTER_VERTICAL,4);
        auto* split=new wxSplitterWindow(browser,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxSP_LIVE_UPDATE|wxSP_3D);
        split->SetMinimumPaneSize(230);split->SetSashGravity(0.35);
        auto* left=new wxPanel(split);paint(left);auto* ls=new wxBoxSizer(wxVERTICAL);left->SetSizer(ls);
        tree=new wxTreeCtrl(left,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxTR_HAS_BUTTONS|wxTR_LINES_AT_ROOT|wxTR_SINGLE);
        paint(tree,false);ls->Add(tree,1,wxEXPAND|wxALL,4);
        auto* right=new wxPanel(split);paint(right);auto* rs=new wxBoxSizer(wxVERTICAL);right->SetSizer(rs);
        selected_label=new wxStaticText(right,wxID_ANY,"Select an asset.",wxDefaultPosition,wxDefaultSize,wxST_ELLIPSIZE_END);
        selected_label->SetForegroundColour(fg);rs->Add(selected_label,0,wxEXPAND|wxALL,6);
        auto* nav=new wxBoxSizer(wxHORIZONTAL);rs->Add(nav,0,wxEXPAND|wxLEFT|wxRIGHT,4);
        prev_button=button(right,nav,prev_id,"<");next_button=button(right,nav,next_id,">");play_button=button(right,nav,play_id,"Play sequence");
        button(right,nav,clear_id,"Clear");
        prev_button->Enable(false);next_button->Enable(false);play_button->Enable(false);
        preview_tabs=new wxNotebook(right,wxID_ANY);rs->Add(preview_tabs,1,wxEXPAND|wxALL,4);
        auto* visual=new wxPanel(preview_tabs);paint(visual);auto* vs=new wxBoxSizer(wxVERTICAL);visual->SetSizer(vs);
        graphic=new Canvas(visual);vs->Add(graphic,1,wxEXPAND|wxALL,4);
        auto* zoom=new wxBoxSizer(wxHORIZONTAL);vs->Add(zoom,0,wxALIGN_CENTER);
        button(visual,zoom,zoom_out,"-");button(visual,zoom,zoom_fit,"Fit");button(visual,zoom,zoom_in,"+");
        preview_tabs->AddPage(visual,"Image");
        auto text_page=[&](const wxString& title){
            auto* p=new wxTextCtrl(preview_tabs,wxID_ANY,"",wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE|wxTE_READONLY|wxTE_DONTWRAP);
            paint(p,false);p->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));preview_tabs->AddPage(p,title);return p;
        };
        raw_hex=text_page("Stored HEX");preview_box=text_page("Decoded HEX");metadata=text_page("Metadata");
        auto* actions=new wxFlexGridSizer(2,2,2,2);rs->Add(actions,0,wxALL,2);
        export_button=button(right,actions,export_one,"Extract raw");export_button->Enable(false);
        decoded_button=button(right,actions,decoded_one,"Extract decoded");decoded_button->Enable(false);
        png_button=button(right,actions,png_one,"Export sprite PNG");png_button->Enable(false);
        group_button=button(right,actions,png_group,"Export group PNG");group_button->Enable(false);
        split->SplitVertically(left,right,350);s->Add(split,1,wxEXPAND|wxALL,4);
        auto* browserBottom=new wxBoxSizer(wxHORIZONTAL);s->Add(browserBottom,0,wxEXPAND);
        button(browser,browserBottom,save_manifest,"Save inventory manifest");
        auto* conversion=page("Extraction / Conversion",s);
        label(conversion,s,"Lossless raw extraction",true);
        label(conversion,s,"Export all parsed FS / FSU members to a fresh folder.\nStored bytes are preserved; Use Asset Browser for decoded exports and sprite PNG bundles.");
        all_button=button(conversion,s,export_all,"Extract all FS / FSU members");all_button->Enable(false);
        auto* valid=page("Validation",s);label(valid,s,"Structural checks",true);
        validation=label(valid,s,"No inventory.");
        button(valid,s,validate_id,"Validate compressed assets");
        auto* logs=page("Log",s);label(logs,s,"Operation log",true);
        log_box=new wxTextCtrl(logs,wxID_ANY,"",wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE|wxTE_READONLY);
        paint(log_box,false);s->Add(log_box,1,wxALL|wxEXPAND,10);
        CreateStatusBar();SetStatusText("Ready - select original Spellcross data");
        Bind(wxEVT_MENU,[this](wxCommandEvent&){choose(true);},open_dir);
        Bind(wxEVT_MENU,[this](wxCommandEvent&){choose(false);},open_file);
        Bind(wxEVT_MENU,[this](wxCommandEvent&){analyze();},scan_id);
        Bind(wxEVT_MENU,[this](wxCommandEvent&){export_manifest();},save_manifest);
        Bind(wxEVT_MENU,[this](wxCommandEvent&){Close();},wxID_EXIT);
        Bind(wxEVT_MENU,[this](wxCommandEvent&){wxMessageBox("Spellcross Asset Converter 0.3.0\nPart of Spellcross Reloaded\nMIT license\nFS format based on Stanislav Maslan's restoration tools.","About",wxOK|wxICON_INFORMATION,this);},wxID_ABOUT);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){choose(true);},open_dir);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){choose(false);},open_file);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){analyze();},scan_id);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_manifest();},save_manifest);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_assets(false);},export_one);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_assets(true);},export_all);
        filter->Bind(wxEVT_TEXT,[this](wxCommandEvent&){refresh();});
        kind->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){refresh();});
        tree->Bind(wxEVT_TREE_SEL_CHANGED,[this](wxTreeEvent&){show_preview();});
        tree->Bind(wxEVT_KEY_DOWN,[this](wxKeyEvent& e){
            if(e.GetKeyCode()==WXK_PAGEUP)step(-1);
            else if(e.GetKeyCode()==WXK_PAGEDOWN)step(1);
            else e.Skip();
        });
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){stop_play();step(-1);},prev_id);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){stop_play();step(1);},next_id);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){
            if(timer.IsRunning())stop_play();
            else if(sequence.size()>1){play_button->SetLabel("Pause sequence");timer.Start(160);}
        },play_id);
        Bind(wxEVT_TIMER,[this](wxTimerEvent&){step(1);},timer.GetId());
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){stop_play();tree->Unselect();show_preview();tree->SetFocus();},clear_id);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){graphic->SetZoom(1);},zoom_in);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){graphic->SetZoom(-1);},zoom_out);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){graphic->SetZoom(0);},zoom_fit);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_png(false);},png_one);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_png(true);},png_group);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_decoded();},decoded_one);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){validate_decoding();},validate_id);
        tabs->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED,[this](wxBookCtrlEvent& e){if(e.GetSelection()!=1)stop_play();e.Skip();});
        Centre();
    }
};
class App final:public wxApp {
public:
    bool OnInit() override {wxInitAllImageHandlers();auto* frame=new Frame;frame->Show();return true;}
};
}
wxIMPLEMENT_APP(App);
