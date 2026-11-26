#include "spellcross/inventory.hpp"
#include "spellcross/graphics.hpp"
#include "spellcross/video.hpp"
#include <wx/sound.h>
#include <chrono>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/scrolwin.h>
#include <wx/dcbuffer.h>
#include <wx/progdlg.h>
#include <wx/timer.h>
#include <wx/image.h>
#include <wx/zipstrm.h>
#include <wx/wfstream.h>
#include <wx/mstream.h>
#include <wx/strconv.h>
#include <array>
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
    prev_id,next_id,play_id,zoom_in,zoom_out,zoom_fit,clear_id,png_one,png_group,validate_id,decoded_one,batch_id,movie_id};
class ItemData final:public wxTreeItemData {
public:
    std::size_t index;
    sc::LayerPath layers;
    bool loaded=false;
    explicit ItemData(std::size_t i,sc::LayerPath p={}):index(i),layers(std::move(p)){}
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
class VideoDialog final:public wxDialog {
    sc::Video movie;
    Canvas* canvas{};
    wxStaticText* status{};
    wxTimer timer;
    wxSound sound;
    std::chrono::steady_clock::time_point started;
    std::size_t current=static_cast<std::size_t>(-1);
    bool playing=false;
    void render(std::size_t i){
        if(i==current)return;current=i;
        wxImage im(movie.width,movie.height);
        auto* rgb=im.GetData();const auto& frame=movie.frames[i];
        const auto& pal=movie.palettes[movie.palettes.size()==1?0:i];
        for(std::size_t p=0;p<frame.size();++p)for(int c=0;c<3;++c)rgb[p*3+c]=pal[frame[p]*3+c];
        canvas->SetImage(im);
        status->SetLabel(number(i+1)+" / "+number(movie.frames.size())+" frames");
    }
    void stop(){timer.Stop();if(playing)wxSound::Stop();playing=false;}
    void play(){
        stop();current=static_cast<std::size_t>(-1);render(0);
        bool audio_ok=false;
        if(!movie.wave.empty()&&sound.Create(movie.wave.size(),movie.wave.data()))audio_ok=sound.Play(wxSOUND_ASYNC);
        started=std::chrono::steady_clock::now();playing=true;timer.Start(15);
        SetTitle(audio_ok?"Video preview":"Video preview (silent / audio unavailable)");
    }
public:
    VideoDialog(wxWindow* parent,sc::Video v):wxDialog(parent,wxID_ANY,"Video preview",wxDefaultPosition,wxSize(860,650),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER),movie(std::move(v)),timer(this){
        auto* s=new wxBoxSizer(wxVERTICAL);SetSizer(s);
        canvas=new Canvas(this);s->Add(canvas,1,wxALL|wxEXPAND,6);
        status=new wxStaticText(this,wxID_ANY,"");s->Add(status,0,wxALL,6);
        auto* controls=new wxBoxSizer(wxHORIZONTAL);s->Add(controls,0,wxALIGN_CENTER|wxALL,6);
        auto* restart=new wxButton(this,wxID_ANY,"Play / restart");controls->Add(restart,0,wxALL,4);
        auto* pause=new wxButton(this,wxID_ANY,"Stop");controls->Add(pause,0,wxALL,4);
        auto* close=new wxButton(this,wxID_CANCEL,"Close");controls->Add(close,0,wxALL,4);
        restart->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){play();});
        pause->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){stop();});
        Bind(wxEVT_TIMER,[this](wxTimerEvent&){
            double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            auto frame=static_cast<std::size_t>(elapsed*movie.fps);
            if(frame>=movie.frames.size()){render(movie.frames.size()-1);stop();}
            else render(frame);
        },timer.GetId());
        render(0);
    }
    ~VideoDialog() override{stop();}
};
wxImage sprite_image(const sc::Sprite& sprite,const sc::Palette& palette,int mode=0){
    if(sprite.width<=0||sprite.height<=0)throw std::runtime_error("Empty sprite");
    wxImage image(sprite.width,sprite.height);
    if(mode==0)image.InitAlpha();
    auto* rgb=image.GetData();auto* alpha=mode==0?image.GetAlpha():nullptr;
    for(std::size_t i=0;i<sprite.indices.size();++i){
        auto idx=sprite.indices[i];
        for(int ch=0;ch<3;++ch)rgb[i*3+ch]=mode==1?idx:mode==2?sprite.opaque[i]:sprite.shadow_index&&idx==253?0:palette.rgb[idx*3+ch];
        if(alpha)alpha[i]=!sprite.opaque[i]?0:sprite.shadow_index&&idx==253?128:255;
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
    wxTextCtrl *metadata{},*raw_hex{},*text_view{};
    wxChoice *block_choice{},*encoding_choice{};
    wxButton* batch_button{};
    wxButton* movie_button{};
    std::map<std::string,std::vector<std::size_t>> blocks;
    std::vector<std::string> block_keys;
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
            rebuild_blocks();refresh();
            SetStatusText("Inventory ready");
            tabs->SetSelection(1);
        } catch(const std::exception& e) {
            stop_play();active.reset();inv={};rebuild_blocks();refresh();
            stats->SetLabel("Scan failed. No active inventory.");
            validation->SetLabel("No inventory.");
            log("ERROR: "+w(e.what()));
            SetStatusText("Scan failed");
            wxMessageBox(w(e.what()),"Cannot scan source",wxOK|wxICON_ERROR,this);
        }
        GetSizer()->Layout();
    }
    wxString decode_text(const sc::Bytes& data){
        if(data.empty())return {};
        const char* p=reinterpret_cast<const char*>(data.data());
        auto enc=encoding_choice->GetStringSelection();
        auto utf=wxString::FromUTF8(p,data.size());
        if((enc=="Auto (UTF-8 / CP895)"||enc=="UTF-8")&&!utf.empty())return utf;
        if(enc=="UTF-8")throw std::runtime_error("Invalid UTF-8");
        if(enc=="CP852"||enc=="Windows-1250"||enc=="Windows-1252"){
            wxCSConv conv(enc=="CP852"?"CP852":enc=="Windows-1250"?"windows-1250":"windows-1252");
            wxString out(p,conv,data.size());
            if(out.empty())throw std::runtime_error("Text conversion failed");
            return out;
        }
        // Kamenicky DOS encoding, as used in the restoration tools.
        static constexpr char32_t cp[]=U"\u010c\u00fc\u00e9\u010f\u00e4\u010e\u0164\u010d\u011b\u011a\u0139\u00cd\u013e\u013a\u00c4\u00c1\u00c9\u017e\u017d\u00f4\u00f6\u00d3\u016f\u00da\u00fd\u00d6\u00dc\u0160\u013d\u00dd\u0158\u0165\u00e1\u00ed\u00f3\u00fa\u0148\u0147\u016e\u00d4\u0161\u0159\u0155\u0154\u00bc\u00a7\u00ab\u00bb\u2591\u2592\u2593\u2502\u2524\u2561\u2562\u2556\u2555\u2563\u2551\u2557\u255d\u255c\u255b\u2510\u2514\u2534\u252c\u251c\u2500\u253c\u255e\u255f\u255a\u2554\u2569\u2566\u2560\u2550\u256c\u2567\u2568\u2564\u2565\u2559\u2558\u2552\u2553\u256b\u256a\u2518\u250c\u2588\u2584\u258c\u2590\u2580\u03b1\u00df\u0393\u03c0\u03a3\u03c3\u00b5\u03c4\u03a6\u0398\u03a9\u03b4\u221e\u03c6\u03b5\u2229\u2261\u00b1\u2265\u2264\u2320\u2321\u00f7\u2248\u00b0\u2219\u00b7\u221a\u207f\u00b2\u25a0 ";
        static_assert(sizeof(cp)/sizeof(cp[0])==129);
        wxString out;
        for(auto b:data)out+=wxUniChar(static_cast<unsigned long>(b<128?b:cp[b-128]));
        return out;
    }
    void rebuild_blocks(){
        blocks.clear();block_keys.clear();block_choice->Clear();block_choice->Append("All game data");
        for(std::size_t i=0;i<inv.assets.size();++i)blocks[sc::block_name(inv.assets[i])].push_back(i);
        for(auto& [key,ids]:blocks){block_keys.push_back(key);block_choice->Append(key.empty()?wxString("Loose files"):w(key));}
        block_choice->SetSelection(0);batch_button->Enable(!blocks.empty());
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
                    tree->SetItemData(parent,new ItemData(i));nodes[i]=parent;++visible;
                    if(sc::can_expand(a,{}))tree->SetItemHasChildren(parent,true);
                    break;
                }
                from=end+1;
            }
        }
        tree->Expand(root);
        if(!needle.empty()||type!="All assets"){
            // Expand matching branches, without expanding an unfiltered full inventory.
            for(auto& [key,item]:folders)if(tree->GetChildrenCount(item,false)>0)tree->Expand(item);
        }
        if(old&&*old<nodes.size()&&nodes[*old].IsOk()){tree->SelectItem(nodes[*old]);tree->EnsureVisible(nodes[*old]);}
        tree->Thaw();rebuilding=false;
        count_label->SetLabel(number(visible)+" matching records");
        all_button->Enable(std::any_of(inv.assets.begin(),inv.assets.end(),[](const auto& a){return a.member;}));
        show_preview();
    }
    ItemData* selected_data(){
        auto item=tree->GetSelection();
        return item.IsOk()?dynamic_cast<ItemData*>(tree->GetItemData(item)):nullptr;
    }
    void expand_layer(wxTreeItemId item){
        if(rebuilding||!item.IsOk())return;
        auto* node=dynamic_cast<ItemData*>(tree->GetItemData(item));
        if(!node||node->loaded||node->index>=inv.assets.size())return;
        const auto& a=inv.assets[node->index];
        if(!sc::can_expand(a,node->layers))return;
        node->loaded=true;
        try{
            auto children=sc::layer_children(a,node->layers);
            for(auto& child:children){
                auto chain=node->layers;chain.push_back(child);
                auto id=tree->AppendItem(item,w(child.label)+" ("+number(child.size)+" B)",-1,-1,new ItemData(node->index,std::move(chain)));
                auto* added=static_cast<ItemData*>(tree->GetItemData(id));
                if(sc::can_expand(a,added->layers))tree->SetItemHasChildren(id,true);
            }
            if(children.empty())tree->SetItemHasChildren(item,false);
        }catch(const std::exception& e){
            tree->AppendItem(item,"Cannot expand: "+w(e.what()));
            log("EXPANSION: "+w(a.name)+": "+w(e.what()));
        }
    }
    const sc::Asset* selected(){
        auto item=tree->GetSelection();
        if(!item.IsOk())return nullptr;
        auto* data=dynamic_cast<ItemData*>(tree->GetItemData(item));
        return data&&data->layers.empty()&&data->index<inv.assets.size()?&inv.assets[data->index]:nullptr;
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
    void show_layer(const ItemData& node){
        active.reset();stop_play();sequence.clear();sequence_pos=0;movie_button->Enable(false);
        prev_button->Enable(false);next_button->Enable(false);play_button->Enable(false);
        png_button->Enable(false);group_button->Enable(false);export_button->Enable(false);decoded_button->Enable(false);
        graphic->SetImage(wxImage());preview_box->Clear();raw_hex->Clear();metadata->Clear();text_view->Clear();
        const auto& a=inv.assets[node.index];
        const auto& last=node.layers.back();
        selected_label->SetLabel(w(a.name)+" / "+w(last.label));
        wxString details="Original: "+w(a.name)+"\n";
        for(auto& layer:node.layers)details+=" -> "+w(layer.label)+" | "+w(layer.format)+" | "+number(layer.size)+" bytes\n";
        try{
            auto data=sc::layer_bytes(a,node.layers);raw_hex->SetValue(hex(data));preview_box->SetValue(hex(data));decoded_button->Enable(true);
            if(auto image=sc::layer_image(a,node.layers,data)){
                if(image->height>0){graphic->SetImage(sprite_image(*image,sc::asset_palette(inv,a,palette)));preview_tabs->SetSelection(0);}
                details+="\nImage: "+number(image->width)+" x "+number(image->height);
            }else if(last.format=="PAL"&&data.size()>=3&&data.size()<=768&&data.size()%3==0){
                int n=static_cast<int>(data.size()/3);wxImage image(256,((n+15)/16)*16);
                image.SetRGB(wxRect(0,0,image.GetWidth(),image.GetHeight()),36,47,35);
                for(int i=0;i<n;++i)image.SetRGB(wxRect(i%16*16,i/16*16,16,16),data[i*3],data[i*3+1],data[i*3+2]);
                graphic->SetImage(image);preview_tabs->SetSelection(0);
            }else if(sc::layer_text(a,node.layers,data)){
                text_view->SetValue(decode_text(data));preview_tabs->SetSelection(4);
            }else preview_tabs->SetSelection(2);
        }catch(const std::exception& e){details+="\nERROR: "+w(e.what());log("LAYER: "+w(e.what()));preview_tabs->SetSelection(3);}
        metadata->SetValue(details);
    }
    void show_preview(){
        if(rebuilding)return;
        if(auto* node=selected_data();node&&!node->layers.empty()&&node->index<inv.assets.size()){show_layer(*node);return;}
        auto* a=selected();
        active=a?std::optional<std::size_t>(a-inv.assets.data()):std::nullopt;
        export_button->Enable(a!=nullptr);movie_button->Enable(a&&sc::is_video(*a));
        png_button->Enable(false);group_button->Enable(false);decoded_button->Enable(false);
        graphic->SetImage(wxImage());preview_box->Clear();raw_hex->Clear();metadata->Clear();text_view->Clear();
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
            if(sc::is_video(*a)){metadata->SetValue(details+"\nUse Play video to decode and preview.");preview_tabs->SetSelection(3);return;}
            auto data=stored;
            if(sc::compressed(*a)){
                data=sc::decode_asset(*a);decoded_button->Enable(true);
                details+="\nDecoded bytes: "+number(data.size());
            }
            preview_box->SetValue(hex(data));
            auto full=data;
            if(!sc::compressed(*a)&&a->size>data.size()&&a->size<=64*1024*1024&&
                (a->format=="ICO"||a->format=="BTN"||a->format=="CUR"||a->format=="GFK"||sc::is_text_asset(*a,data)))
                full=sc::decode_asset(*a);
            if(auto decoded=sc::decode_image(*a,full)){
                auto s=std::move(*decoded);
                if(s.height>0){
                    graphic->SetImage(sprite_image(s,sc::asset_palette(inv,*a,palette)));png_button->Enable(a->format=="FSU-SPRITE");group_button->Enable(a->format=="FSU-SPRITE");preview_tabs->SetSelection(0);
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
                graphic->SetImage(image);preview_tabs->SetSelection(0);
            }else if(sc::is_text_asset(*a,full)&&full.size()==a->size){
                text_view->SetValue(decode_text(full));preview_tabs->SetSelection(4);
            }else if(sc::compressed(*a)){preview_tabs->SetSelection(2);}
            else{preview_tabs->SetSelection(1);}
        }catch(const std::exception& e){details+="\nERROR: "+w(e.what());log("ERROR: "+w(e.what()));preview_tabs->SetSelection(3);}
        metadata->SetValue(details);
    }
    void play_video(){
        stop_play();auto* a=selected();if(!a||!sc::is_video(*a))return;
        try{
            sc::Video movie;
            {wxBusyCursor busy;movie=sc::decode_video(inv,*a);}
            log("VIDEO: "+w(a->name)+" | "+number(movie.frames.size())+" frames | "+w(movie.audio_note));
            VideoDialog dialog(this,std::move(movie));dialog.ShowModal();
        }catch(const std::exception& e){log("VIDEO ERROR: "+w(e.what()));wxMessageBox(w(e.what()),"Video decode failed",wxOK|wxICON_ERROR,this);}
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
        stop_play();auto* node=selected_data();if(!node||node->index>=inv.assets.size())return;
        const auto a=inv.assets[node->index];const auto layers=node->layers;
        wxDirDialog d(this,"Choose folder for decoded data");
        if(d.ShowModal()!=wxID_OK)return;
        try{
            auto data=layers.empty()?sc::decode_asset(a):sc::layer_bytes(a,layers);
            auto dest=fresh_dir(path(d.GetPath()),"Spellcross-decoded-");
            write_bytes(dest/"decoded.bin",data);
            write_bytes(dest/"original.bin",sc::preview(a,static_cast<std::size_t>(a.size)));
            log("Decoded data exported: "+w(sc::utf8(dest)));
            wxMessageBox(w(sc::utf8(dest)),"Decoded export",wxOK|wxICON_INFORMATION,this);
        }catch(const std::exception& e){log("ERROR: "+w(e.what()));wxMessageBox(w(e.what()),"Export failed",wxOK|wxICON_ERROR,this);}
    }
    static void zip_bytes(wxZipOutputStream& z,const wxString& name,const void* bytes,std::size_t size){
        if(!z.PutNextEntry(name))throw std::runtime_error("Cannot create ZIP entry");
        if(size){z.Write(bytes,size);if(z.LastWrite()!=size||!z.IsOk())throw std::runtime_error("ZIP write failed");}
        if(!z.CloseEntry())throw std::runtime_error("ZIP entry close failed");
    }
    static void zip_string(wxZipOutputStream& z,const wxString& name,const std::string& value){
        zip_bytes(z,name,value.data(),value.size());
    }
    static void zip_original(wxZipOutputStream& z,const wxString& name,const sc::Asset& a){
        std::ifstream in(a.backing,std::ios::binary);
        if(!in||!in.seekg(static_cast<std::streamoff>(a.offset)))throw std::runtime_error("Cannot read original asset");
        if(!z.PutNextEntry(name))throw std::runtime_error("Cannot create original ZIP entry");
        std::array<char,65536> buffer{};
        for(std::uint64_t left=a.size;left;){
            auto n=static_cast<std::size_t>(std::min<std::uint64_t>(left,buffer.size()));
            if(!in.read(buffer.data(),static_cast<std::streamsize>(n)))throw std::runtime_error("Original asset read failed");
            z.Write(buffer.data(),n);
            if(z.LastWrite()!=n||!z.IsOk())throw std::runtime_error("Original ZIP write failed");
            left-=n;
        }
        if(!z.CloseEntry())throw std::runtime_error("Original ZIP entry close failed");
    }
    static sc::Bytes png_bytes(const wxImage& im){
        wxMemoryOutputStream out;
        if(!im.SaveFile(out,wxBITMAP_TYPE_PNG))throw std::runtime_error("PNG encoding failed");
        sc::Bytes bytes(out.GetSize());out.CopyTo(bytes.data(),bytes.size());return bytes;
    }
    static std::string tsv(const std::string& value){
        std::string out;
        for(char ch:value){if(ch=='\\')out+="\\\\";else if(ch=='\t')out+="\\t";else if(ch=='\r')out+="\\r";else if(ch=='\n')out+="\\n";else out+=ch;}
        return out;
    }
    void convert_blocks(){
        stop_play();if(blocks.empty())return;
        wxDirDialog d(this,"Choose destination for converted ZIP bundles");
        if(d.ShowModal()!=wxID_OK)return;
        std::vector<std::string> keys;
        int choice=block_choice->GetSelection();
        if(choice<=0)keys=block_keys;else keys.push_back(block_keys.at(static_cast<std::size_t>(choice-1)));
        std::size_t total=0;for(auto& key:keys)total+=blocks.at(key).size();
        if(!total)return;
        std::filesystem::path dest;
        try{
            dest=fresh_dir(path(d.GetPath()),"Spellcross-modern-");
            wxProgressDialog progress("Convert game data","Packaging archive blocks...",static_cast<int>(total),this,wxPD_APP_MODAL|wxPD_CAN_ABORT|wxPD_AUTO_HIDE);
            std::size_t done=0,converted=0,decoded=0,raw=0,failed=0,block_number=0;
            std::ostringstream catalogue;catalogue<<"zip\tlogical_block\tassets\n";
            for(auto& key:keys){
                const auto& ids=blocks.at(key);
                std::string basename=key.empty()?"loose-files":key.substr(key.find_last_of("/\\")==std::string::npos?0:key.find_last_of("/\\")+1);
                for(auto& ch:basename)if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.'))ch='_';
                if(basename.size()>80)basename.resize(80);
                std::string filename=std::to_string(++block_number)+"_"+basename+".zip";
                wxFFileOutputStream file(w(sc::utf8(dest/filename)));
                if(!file.IsOk())throw std::runtime_error("Cannot open ZIP output");
                wxZipOutputStream zip(file);
                zip.SetFormat(wxZIP_FORMAT_ZIP64);
                std::ostringstream manifest;manifest<<"id\tlogical_name\tformat\tstored_size\tstatus\tdetail\n";
                for(auto id:ids){
                    const auto& a=inv.assets[id];
                    if(done%16==0&&!progress.Update(static_cast<int>(done),w(a.name)))throw std::runtime_error("Cancelled; incomplete export removed");
                    wxString prefix="assets/"+number(id)+"/";
                    zip_original(zip,prefix+"original.bin",a);
                    std::string status="raw",detail="No modern decoder for this format";
                    std::vector<std::pair<wxString,sc::Bytes>> derived;
                    auto add_text=[&](const wxString& n,const std::string& v){derived.emplace_back(n,sc::Bytes(v.begin(),v.end()));};
                    try{
                        if(a.format=="FS"||a.format=="FSU"){detail="Original container retained with its catalogued members";}
                        else if(a.size<=64*1024*1024){
                            auto data=sc::decode_asset(a);
                            if(auto image=sc::decode_image(a,data)){
                                auto p=sc::asset_palette(inv,a,palette);
                                const char* names[]={"preview.png","indices.png","opacity.png"};
                                for(int i=0;i<3;++i)derived.emplace_back(names[i],png_bytes(sprite_image(*image,p,i)));
                                add_text("sprite.json",sc::sprite_metadata(a,*image,p));
                                status="converted";detail="Indexed image and metadata";
                            }else if(sc::is_text_asset(a,data)){
                                auto utf=decode_text(data).utf8_str();
                                add_text("text.txt",std::string(utf.data(),utf.length()));
                                status="converted";detail="UTF-8 text";
                            }else if(a.format=="PAL"&&data.size()>=3&&data.size()<=768&&data.size()%3==0){
                                std::ostringstream p;p<<"{\"rgb\":[";
                                for(std::size_t i=0;i<data.size();++i){if(i)p<<',';p<<unsigned(data[i]);}p<<"]}\n";
                                add_text("palette.json",p.str());status="converted";detail="RGB palette";
                            }else if(sc::compressed(a)){derived.emplace_back("decoded.bin",std::move(data));status="decoded";detail="Decompressed; inner format not interpreted";}
                        }else detail="Asset exceeds decoder memory limit; original retained";
                    }catch(const std::exception& e){derived.clear();status="failed";detail=e.what();log("CONVERSION: "+w(a.name)+": "+w(detail));}
                    for(auto& [name,bytes]:derived)zip_bytes(zip,prefix+name,bytes.data(),bytes.size());
                    if(status=="converted")++converted;else if(status=="decoded")++decoded;else if(status=="failed")++failed;else ++raw;
                    manifest<<id<<'\t'<<tsv(a.name)<<'\t'<<tsv(a.format)<<'\t'<<a.size<<'\t'<<status<<'\t'<<tsv(detail)<<'\n';
                    ++done;
                }
                zip_string(zip,"manifest.tsv",manifest.str());
                if(!zip.Close())throw std::runtime_error("ZIP finalization failed");
                if(!file.Close())throw std::runtime_error("ZIP file close failed");
                catalogue<<filename<<'\t'<<tsv(key.empty()?"(loose files)":key)<<'\t'<<ids.size()<<'\n';
            }
            std::ofstream index(dest/"blocks.tsv",std::ios::binary);index<<catalogue.str();index.close();
            if(!index)throw std::runtime_error("Block catalogue write failed");
            std::ofstream warnings(dest/"scan-warnings.txt",std::ios::binary);for(auto& warning:inv.warnings)warnings<<warning<<'\n';warnings.close();
            if(!warnings)throw std::runtime_error("Warning log write failed");
            progress.Update(static_cast<int>(total));
            auto summary=number(keys.size())+" ZIP bundle(s)\n"+number(converted)+" converted, "+number(decoded)+" decoded, "+number(raw)+" original-only, "+number(failed)+" conversion failures (original retained).\n"+w(sc::utf8(dest));
            log(summary);wxMessageBox(summary,"Conversion finished",wxOK|wxICON_INFORMATION,this);
        }catch(const std::exception& e){
            if(!dest.empty()){std::error_code ec;std::filesystem::remove_all(dest,ec);if(ec)log("ERROR: Could not remove incomplete export "+w(sc::utf8(dest)));}
            log("ERROR: "+w(e.what()));wxMessageBox(w(e.what()),"Conversion interrupted",wxOK|wxICON_ERROR,this);
        }
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
        label(header,hs,"RELOADED TOOLKIT   |   v0.5.0   |   Original data inventory");
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
        movie_button=button(right,nav,movie_id,"Play video");movie_button->Enable(false);
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
        raw_hex=text_page("Stored HEX");preview_box=text_page("Decoded HEX");metadata=text_page("Metadata");text_view=text_page("Text");
        encoding_choice=new wxChoice(right,wxID_ANY);for(auto enc:{"Auto (UTF-8 / CP895)","UTF-8","CP895","CP852","Windows-1250","Windows-1252"})encoding_choice->Append(enc);
        encoding_choice->SetSelection(0);rs->Add(encoding_choice,0,wxEXPAND|wxLEFT|wxRIGHT,6);
        encoding_choice->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){show_preview();});
        auto* actions=new wxFlexGridSizer(2,2,2,2);rs->Add(actions,0,wxALL,2);
        export_button=button(right,actions,export_one,"Extract raw");export_button->Enable(false);
        decoded_button=button(right,actions,decoded_one,"Extract decoded");decoded_button->Enable(false);
        png_button=button(right,actions,png_one,"Export sprite PNG");png_button->Enable(false);
        group_button=button(right,actions,png_group,"Export group PNG");group_button->Enable(false);
        split->SplitVertically(left,right,350);s->Add(split,1,wxEXPAND|wxALL,4);
        auto* browserBottom=new wxBoxSizer(wxHORIZONTAL);s->Add(browserBottom,0,wxEXPAND);
        button(browser,browserBottom,save_manifest,"Save inventory manifest");
        auto* conversion=page("Extraction / Conversion",s);
        label(conversion,s,"Convert game data to ZIP bundles",true);
        label(conversion,s,"Choose all game data or one logical archive.\nSupported assets are converted automatically; original bytes are always retained.");
        block_choice=new wxChoice(conversion,wxID_ANY);block_choice->Append("All game data");block_choice->SetSelection(0);s->Add(block_choice,0,wxEXPAND|wxALL,10);
        batch_button=button(conversion,s,batch_id,"Convert and package");batch_button->Enable(false);
        label(conversion,s,"Lossless raw extraction",true);
        label(conversion,s,"Export all parsed FS / FSU members to a fresh folder.\nStored bytes are preserved; For individual exports use Asset Browser.");
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
        Bind(wxEVT_MENU,[this](wxCommandEvent&){wxMessageBox("Spellcross Asset Converter 0.5.0\nPart of Spellcross Reloaded\nMIT license\nCopyright (c) 2026 Luboš Horák\nSpellcross Reloaded by Luboš Horák; foundations by Stanislav Mašláň.\nhttps://github.com/luboshorak/spellcross-reloaded\nCAN decoder: CPAL-1.0; libsmacker: LGPL-2.1-or-later.","About",wxOK|wxICON_INFORMATION,this);},wxID_ABOUT);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){choose(true);},open_dir);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){choose(false);},open_file);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){analyze();},scan_id);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_manifest();},save_manifest);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_assets(false);},export_one);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){export_assets(true);},export_all);
        filter->Bind(wxEVT_TEXT,[this](wxCommandEvent&){refresh();});
        kind->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){refresh();});
        tree->Bind(wxEVT_TREE_SEL_CHANGED,[this](wxTreeEvent&){show_preview();});
        tree->Bind(wxEVT_TREE_ITEM_EXPANDING,[this](wxTreeEvent& e){expand_layer(e.GetItem());e.Skip();});
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
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){convert_blocks();},batch_id);
        Bind(wxEVT_BUTTON,[this](wxCommandEvent&){play_video();},movie_id);
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
