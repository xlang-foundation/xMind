#include "agentflow/file_patch.hpp"
#include <algorithm>
#include <set>

namespace agentflow {
namespace {
constexpr std::size_t patch_limit=65536,file_limit=1024*1024;
void text_valid(std::string_view text){
 for(std::size_t i=0;i<text.size();){
  const auto first=static_cast<unsigned char>(text[i++]);
  if(first==0)throw PatchInvalid("Patch text contains a NUL byte");
  if(first<128)continue;
  unsigned extra=0,value=0,minimum=0;
  if(first>=0xc2&&first<=0xdf){extra=1;value=first&31;minimum=0x80;}
  else if(first>=0xe0&&first<=0xef){extra=2;value=first&15;minimum=0x800;}
  else if(first>=0xf0&&first<=0xf4){extra=3;value=first&7;minimum=0x10000;}
  else throw PatchInvalid("Patch text is not UTF-8");
  if(extra>text.size()-i)throw PatchInvalid("Patch text is not UTF-8");
  while(extra--){const auto next=static_cast<unsigned char>(text[i++]);if((next&0xc0)!=0x80)throw PatchInvalid("Patch text is not UTF-8");value=(value<<6)|(next&63);}
  if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))throw PatchInvalid("Patch text is not UTF-8");
 }
}
struct Line {std::string_view text;std::size_t start,next;};
std::vector<Line> split(std::string_view text){
 std::vector<Line> lines;std::size_t start=0;
 while(start<text.size()){
  auto end=text.find('\n',start);const bool terminated=end!=std::string_view::npos;
  if(!terminated)end=text.size();auto content=end;
  if(terminated&&content>start&&text[content-1]=='\r')--content;
  lines.push_back({text.substr(start,content-start),start,terminated?end+1:end});start=terminated?end+1:end;
 }
 return lines;
}
std::string path_name(std::string_view raw){
 if(raw.empty()||raw.size()>4096)throw PatchInvalid("Invalid patch path");
 std::string path(raw);std::replace(path.begin(),path.end(),'\\','/');
 if(path.front()=='/'||path.back()=='/')throw PatchInvalid("Patch paths must name relative files");
 for(const auto c:path)if(static_cast<unsigned char>(c)<32||c==127||c==':')throw PatchInvalid("Invalid patch path");
 std::size_t start=0;
 while(start<path.size()){
  auto end=path.find('/',start);if(end==std::string::npos)end=path.size();const auto part=std::string_view(path).substr(start,end-start);
  if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')throw PatchInvalid("Invalid patch path component");
  start=end+1;
 }
 return path;
}
std::string folded(std::string name){for(auto& c:name)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return name;}
bool header(std::string_view line){return line.starts_with("*** ");}
}
std::vector<FilePatch> parse_file_patch(std::string_view source){
 if(source.empty()||source.size()>patch_limit)throw PatchInvalid("Patch exceeds input bounds");text_valid(source);
 auto lines=split(source);
 if(lines.size()<3||lines.front().text!="*** Begin Patch"||lines.back().text!="*** End Patch")throw PatchInvalid("Patch envelope is invalid");
 std::vector<FilePatch> result;std::set<std::string> paths;std::size_t i=1,total_chunks=0;
 auto reserve_path=[&](const std::string& path){if(!paths.insert(folded(path)).second)throw PatchInvalid("A patch path is used more than once");};
 while(i+1<lines.size()){
  if(result.size()>=128)throw PatchInvalid("Patch contains too many files");
  const auto title=lines[i++].text;FilePatch file;
  if(title.starts_with("*** Add File: ")){file.action=PatchAction::add;file.path=path_name(title.substr(14));}
  else if(title.starts_with("*** Delete File: ")){file.action=PatchAction::remove;file.path=path_name(title.substr(17));}
  else if(title.starts_with("*** Update File: ")){file.action=PatchAction::update;file.path=path_name(title.substr(17));}
  else throw PatchInvalid("Invalid patch file header");
  reserve_path(file.path);
  if(file.action==PatchAction::add){
   while(i+1<lines.size()&&!header(lines[i].text)){
    const auto line=lines[i++].text;if(line.empty()||line.front()!='+')throw PatchInvalid("Invalid added-file line");
    file.content.append(line.substr(1));file.content.push_back('\n');
   }
  }else if(file.action==PatchAction::update){
   if(i+1<lines.size()&&lines[i].text.starts_with("*** Move to: ")){file.move_to=path_name(lines[i++].text.substr(13));reserve_path(file.move_to);}
   while(i+1<lines.size()&&!header(lines[i].text)){
    if(file.chunks.size()>=256||++total_chunks>1024)throw PatchInvalid("Patch contains too many chunks");
    PatchChunk chunk;
    if(lines[i].text=="@@")++i;
    else if(lines[i].text.starts_with("@@ "))chunk.anchor=std::string(lines[i++].text.substr(3));
    while(i+1<lines.size()&&!header(lines[i].text)&&lines[i].text!="@@"&&!lines[i].text.starts_with("@@ ")){
     const auto line=lines[i++].text;if(line.empty()||(line.front()!=' '&&line.front()!='+'&&line.front()!='-'))throw PatchInvalid("Invalid update line");
     chunk.lines.push_back({line.front(),std::string(line.substr(1))});
    }
    if(chunk.lines.empty()||std::none_of(chunk.lines.begin(),chunk.lines.end(),[](const auto& line){return line.kind!=' ';}))throw PatchInvalid("Patch chunk has no changes");
    if(i+1<lines.size()&&lines[i].text=="*** End of File"){chunk.end_of_file=true;++i;}
    file.chunks.push_back(std::move(chunk));
   }
   if(file.chunks.empty())throw PatchInvalid("Updated file has no chunks");
  }
  result.push_back(std::move(file));
 }
 if(result.empty())throw PatchInvalid("Patch contains no files");return result;
}
std::string prepare_patch_update(std::string_view before,const FilePatch& patch,std::stop_token cancel){
 auto cancelled=[&]{if(cancel.stop_requested())throw PatchCancelled("Patch preparation cancelled");};cancelled();
 if(patch.action!=PatchAction::update||patch.chunks.empty()||before.size()>file_limit)throw PatchInvalid("Invalid patch update input");text_valid(before);
 const auto lines=split(before);const std::string_view newline=before.find("\r\n")!=std::string_view::npos?"\r\n":"\n";
 std::size_t cursor=0,byte_cursor=0;std::string after;
 auto append=[&](std::string_view bytes){if(bytes.size()>file_limit-after.size())throw PatchInvalid("Patched file exceeds bounds");after.append(bytes);};
 for(const auto& chunk:patch.chunks){
  cancelled();
  if(!chunk.anchor.empty()){
   auto found=std::find_if(lines.begin()+cursor,lines.end(),[&](const auto& line){return line.text==chunk.anchor;});
   if(found==lines.end())throw PatchConflict("Patch anchor was not found");cursor=static_cast<std::size_t>(found-lines.begin())+1;
  }
  std::vector<std::string_view> old;for(const auto& line:chunk.lines)if(line.kind!='+')old.push_back(line.text);
  if(old.empty()&&!chunk.end_of_file)throw PatchConflict("Insertion requires explicit end-of-file context");
  std::size_t match=old.empty()?lines.size():lines.size()+1;
  // KMP avoids rescanning a long repeated context at every source line.
  if(!old.empty()){
   std::vector<std::size_t> prefix(old.size());std::size_t matched=0;
   for(std::size_t n=1;n<old.size();++n){while(matched&&old[n]!=old[matched])matched=prefix[matched-1];if(old[n]==old[matched])++matched;prefix[n]=matched;}
   matched=0;
   for(std::size_t n=cursor;n<lines.size();++n){
    cancelled();while(matched&&lines[n].text!=old[matched])matched=prefix[matched-1];if(lines[n].text==old[matched])++matched;
    if(matched==old.size()){
     if(!chunk.end_of_file||n+1==lines.size()){if(match<=lines.size())throw PatchConflict("Patch context matches more than one location");match=n+1-old.size();}
     matched=prefix[matched-1];
    }
   }
  }
  if(match>lines.size())throw PatchConflict("Patch context was not found");
  const auto start=match<lines.size()?lines[match].start:before.size();append(before.substr(byte_cursor,start-byte_cursor));
  auto old_at=match;std::string replacement;
  for(const auto& line:chunk.lines){
   if(line.kind=='+'){
    if(!replacement.empty()&&replacement.back()!='\n')replacement.append(newline);
    replacement.append(line.text);replacement.append(newline);
   }else if(line.kind==' '){replacement.append(before.substr(lines[old_at].start,lines[old_at].next-lines[old_at].start));++old_at;}
   else ++old_at;
  }
  const bool ends_at_eof=old_at==lines.size();
  if(ends_at_eof&&!before.empty()&&before.back()!='\n'&&replacement.ends_with(newline))replacement.resize(replacement.size()-newline.size());
  if(old.empty()&&!after.empty()&&after.back()!='\n'&&!replacement.empty())append(newline);
  append(replacement);byte_cursor=old_at?lines[old_at-1].next:start;cursor=old_at;
 }
 append(before.substr(byte_cursor));return after;
}
}
