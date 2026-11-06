#include <3ds.h>
#include <simplesquirrel/vm.hpp>
#include <cstdio>
#include <cstdarg>
#include <string>
static FILE* report=nullptr;
static void print(HSQUIRRELVM,const char* fmt,...) {va_list args;va_start(args,fmt);vfprintf(report,fmt,args);va_end(args);fflush(report);}
int main() {
  gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);
  report=fopen("sdmc:/3ds/supertux070/vm-results.txt","w");
  try {
    ssq::VM vm(64,ssq::Libs::MATH|ssq::Libs::STRING);vm.setPrintFunc(&print,&print);
    vm.addFunc("native_int",[]()->int{return 45;});
    vm.addFunc("bound_raw",[](HSQUIRRELVM v)->SQInteger{sq_pushinteger(v,73);return 1;});
    vm.addFunc("native_float",[]()->float{return 45.f;});
    vm.addFunc("native_plus",[](int x)->int{return x+1;});
    auto h=vm.getHandle();
    sq_pushroottable(h);sq_pushstring(h,"raw_int",-1);
    sq_newclosure(h,[](HSQUIRRELVM v)->SQInteger {sq_pushinteger(v,45);return 1;},0);
    sq_newslot(h,-3,SQFalse);sq_pop(h,1);
    fprintf(report,"SQInteger %u bytes\n",unsigned(sizeof(SQInteger)));
    const char* source=R"SQ(
      print("raw return: "+typeof raw_int()+" "+raw_int()+"\n");
      print("float return: "+typeof native_float()+" "+native_float()+"\n");
      print("arg return: "+typeof native_plus(2)+" "+native_plus(2)+"\n");
      assert(typeof native_int()=="integer" && native_int()==45);
      assert(native_plus(2)==3 && bound_raw()==73 && raw_int()==45);
      for(local i=0;i<128;++i) assert(3607.0+(native_int()%6)-3==3607.0);
      local a=45%6; print("literal modulo: "+typeof a+" "+a+"\n");
      local b=45+3; print("literal plus: "+typeof b+" "+b+"\n");
      local c=native_int();print("native return: "+typeof c+" "+c+"\n");
      local d=c%6; print("native modulo: "+typeof d+" "+d+"\n");
      local e=3607.0+d-3; print("float add: "+typeof e+" "+e+"\n");
    )SQ";
    vm.run(vm.compileSource(source,"numeric-probe"));fprintf(report,"PASS\n");
  } catch(const std::exception& e){fprintf(report,"FAIL %s\n",e.what());}
  fclose(report);
  while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_START)break;gspWaitForVBlank();}
  gfxExit();return 0;
}

