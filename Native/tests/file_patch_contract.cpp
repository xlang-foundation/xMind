#include "agentflow/file_patch.hpp"
#include <iostream>
#include <functional>
using namespace agentflow;
namespace {
void require(bool value){if(!value)throw std::runtime_error("Patch contract failed");}
template<class Error>void rejects(const std::function<void()>& action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected patch rejection");}
FilePatch update(const std::string& body){return parse_file_patch("*** Begin Patch\n*** Update File: src/file.txt\n"+body+"*** End Patch\n").at(0);}
}
int main(){try{
 const auto files=parse_file_patch("*** Begin Patch\r\n*** Add File: src/new.txt\r\n+中\r\n+\r\n*** Update File: src/old.txt\r\n*** Move to: src/moved.txt\r\n@@ anchor\r\n-old\r\n+new\r\n*** Delete File: obsolete.txt\r\n*** End Patch\r\n");
 require(files.size()==3&&files[0].action==PatchAction::add&&files[0].content=="中\n\n"&&files[1].move_to=="src/moved.txt"&&files[2].action==PatchAction::remove);
 require(prepare_patch_update("anchor\r\nold\r\ntail\n",files[1])=="anchor\r\nnew\r\ntail\n");
 require(prepare_patch_update("lead\nold\ntail\n",update("@@\n-old\n+new\n"))=="lead\nnew\ntail\n");
 require(prepare_patch_update("lead\nold",update("@@\n-old\n+new\n*** End of File\n"))=="lead\nnew");
 require(prepare_patch_update("old\n",update("@@\n-old\n"))=="");
 require(prepare_patch_update("",update("@@\n+first\n*** End of File\n"))=="first\n");
 require(prepare_patch_update("tail",update("@@\n+next\n*** End of File\n"))=="tail\nnext");
 require(prepare_patch_update("tail",update("@@\n tail\n+next\n*** End of File\n"))=="tail\nnext");
 require(prepare_patch_update("old\nold\n",update("@@\n-old\n+new\n*** End of File\n"))=="old\nnew\n");
 require(prepare_patch_update("a\nb\nc\nd\n",update("@@\n-a\n+A\n@@\n-d\n+D\n"))=="A\nb\nc\nD\n");
 rejects<PatchConflict>([]{prepare_patch_update("old\nold\n",update("@@\n-old\n+new\n"));});
 rejects<PatchConflict>([]{prepare_patch_update("missing\n",update("@@\n-old\n+new\n"));});
 rejects<PatchConflict>([]{prepare_patch_update("tail\n",update("@@\n+unanchored\n"));});
 rejects<PatchConflict>([]{prepare_patch_update("old\ntail\n",update("@@\n-old\n+new\n*** End of File\n"));});
 rejects<PatchConflict>([]{prepare_patch_update("old\n",update("@@ absent\n-old\n+new\n"));});
 for(const auto& text:{"", "*** Begin Patch\n*** End Patch\n", "*** Begin Patch\n*** Add File: ../escape\n+x\n*** End Patch\n", "*** Begin Patch\n*** Add File: /escape\n+x\n*** End Patch\n", "*** Begin Patch\n*** Add File: C:/escape\n+x\n*** End Patch\n", "*** Begin Patch\n*** Add File: a\nraw\n*** End Patch\n", "*** Begin Patch\n*** Update File: a\n@@\n unchanged\n*** End Patch\n", "*** Begin Patch\n*** Delete File: a\n*** Add File: A\n+x\n*** End Patch\n", "*** Begin Patch\n*** Update File: a\n*** Move to: a\n@@\n-x\n+y\n*** End Patch\n"})rejects<PatchInvalid>([&]{parse_file_patch(text);});
 rejects<PatchInvalid>([]{parse_file_patch(std::string(65537,'x'));});
 rejects<PatchInvalid>([]{std::string patch="*** Begin Patch\n";for(int n=0;n<129;++n)patch+="*** Delete File: file"+std::to_string(n)+"\n";parse_file_patch(patch+"*** End Patch\n");});
 rejects<PatchInvalid>([]{parse_file_patch(std::string("*** Begin Patch\n*** Add File: a\n+")+char(0xc0)+char(0x80)+"\n*** End Patch\n");});
 std::stop_source stopped;stopped.request_stop();rejects<PatchCancelled>([&]{prepare_patch_update("old\n",update("@@\n-old\n+new\n"),stopped.get_token());});
 std::string repeated;for(int n=0;n<100000;++n)repeated+="x\n";repeated+="last\n";
 require(prepare_patch_update(repeated,update("@@\n x\n-last\n+done\n*** End of File\n")).ends_with("x\ndone\n"));
 std::cout<<"Native patch parse/preparation contract passed; no filesystem effects or approval dispatch\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
