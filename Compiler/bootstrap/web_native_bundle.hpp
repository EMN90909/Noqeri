// Stage-0 native web application bundler. Generated app binary embeds the
// Noqeri source and the library component declarations (no runtime files).
// This compiles the bootstrap renderer into a native executable, NOT typed NIR.
#pragma once
#if !defined(_WIN32)
std::string cppRawLiteral(const std::string& source) {
    // Raw literal must use a delimiter no longer than 16 characters.
    for(unsigned long attempt=0;attempt<100000;attempt++) {
        const auto delimiter="NQ"+std::to_string(attempt);
        if(source.find(")"+delimiter+"\"")==std::string::npos)
            return "R\""+delimiter+"("+source+")"+delimiter+"\"";
    }
    throw std::runtime_error("could not safely encode Noqeri source in a C++ literal");
}
fs::path nativeWebCompilerSource(const char* executable) {
    std::vector<fs::path> candidates={
        fs::path(__FILE__),
        fs::current_path()/"Compiler/bootstrap/web_native.cpp",
        fs::absolute(executable).parent_path()/"../share/noqeri/Compiler/bootstrap/web_native.cpp",
        fs::absolute(executable).parent_path()/"../Compiler/bootstrap/web_native.cpp"
    };
    if(const char* configured=std::getenv("NOQERI_WEB_COMPILER_SOURCE"))
        candidates.insert(candidates.begin(),fs::path(configured));
    for(const auto& candidate:candidates) {
        std::error_code error;
        if(fs::is_regular_file(candidate,error))return fs::canonical(candidate);
    }
    throw std::runtime_error("native web bootstrap source not installed; set NOQERI_WEB_COMPILER_SOURCE");
}
void compileApplication(const fs::path& input,const fs::path& executable,const char* self) {
    const auto source=readText(fs::weakly_canonical(input)/"main.noqeri");
    Project project;
    Parser(source,project).parse();
    if(project.routes.empty())throw std::runtime_error("compiled app requires routes");
    const auto ui=project.uiAlias.empty()?std::string():readText(uiSource(self));
    if(!ui.empty())Parser(ui,project,true).parse();
    for(const auto& [route,nodes]:project.routes) {
        if(!validRoute(route))throw std::runtime_error("invalid compiled route "+route);
        std::map<std::string,const Node*> actions;
        validateActions(nodes,project,actions);
    }
    if(project.hasLayout&&layoutSlotCount(project.layout)!=1)
        throw std::runtime_error("compiled layout requires exactly one slot {}");
    // The compiler/runtime source is included directly as the generator ABI.
    const auto engine=nativeWebCompilerSource(self);
    // Include path cannot contain quotes or control characters; unlike source
    // contents it is not a raw literal.
    const auto path=engine.string();
    if(path.find('"')!=std::string::npos||path.find('\n')!=std::string::npos||
       path.find('\\')!=std::string::npos)
        throw std::runtime_error("unsafe compiler source path for embedded build");
    const auto destination=fs::absolute(executable).lexically_normal();
    if(destination==fs::weakly_canonical(input/"main.noqeri"))
        throw std::runtime_error("output would replace Noqeri application source");
    const auto staging=fs::path(destination.string()+".cpp");
    const auto partial=fs::path(destination.string()+".pending");
    const auto generated=
        "#define NOQERI_WEB_BUNDLED 1\n"
        "#include \""+path+"\"\n"
        "int main(int argc,char** argv) {\n"
        "  try {\n"
        "    const std::string source="+cppRawLiteral(source)+";\n"
        "    const std::string ui="+cppRawLiteral(ui)+";\n"
        "    if(argc>3)throw std::runtime_error(\"usage: app [port] [state-file]\");\n"
        "    int port=8080;\n"
        "    if(argc>=2) {\n"
        "      const std::string digits=argv[1];\n"
        "      if(digits.empty()||!std::all_of(digits.begin(),digits.end(),"
        "[](char c){return c>='0'&&c<='9';}))throw std::runtime_error(\"invalid port\");\n"
        "      port=std::stoi(digits);\n"
        "    }\n"
        "    serveApp(fs::current_path(),port,argc==3?fs::path(argv[2]):"
        "fs::path(\"noqeri-data/app-state.nqkv\"),argv[0],&source,&ui);\n"
        "  }catch(const std::exception& e){std::cerr<<\"Noqeri app: \"<<e.what()<<'\\n';return 1;}\n"
        "}\n";
    try {
        writeText(staging,generated);
        const char* env=std::getenv("CXX");
        const std::string cxx=env&&*env?env:"c++";
        if(cxx.find(' ')!=std::string::npos)
            throw std::runtime_error("CXX must name one executable, not shell flags");
        const pid_t pid=::fork();
        if(pid<0)throw std::runtime_error("failed creating native compiler process");
        if(pid==0) {
            ::execlp(cxx.c_str(),cxx.c_str(),"-std=c++17","-O2","-pthread",
                     staging.c_str(),"-o",partial.c_str(),static_cast<char*>(nullptr));
            _exit(127);
        }
        int status=0;
        if(::waitpid(pid,&status,0)<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0)
            throw std::runtime_error("native C++ bootstrap compilation failed");
        fs::rename(partial,destination);
        std::error_code ignored;
        fs::remove(staging,ignored);
    }catch(...) {
        std::error_code ignored;
        fs::remove(partial,ignored);
        fs::remove(staging,ignored);
        throw;
    }
    std::cout<<"noqeri-web: compiled native self-contained bootstrap app "<<destination<<"\n";
}
#endif
