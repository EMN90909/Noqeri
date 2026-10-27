// SPDX-License-Identifier: GPL-3.0-only
// Optional durable native sessions for multiple processes sharing one SQLite WAL.
// Not a multi-host distributed session service. Requires NOQERI_WEB_SQLITE.
#pragma once
#if defined(NOQERI_WEB_SQLITE) && !defined(_WIN32)
struct NativeSqliteSessions {
    sqlite3* handle=nullptr;
    explicit NativeSqliteSessions(const fs::path& file) {
        fs::create_directories(file.parent_path());
        if(sqlite3_open_v2(file.string().c_str(),&handle,
           SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)
            throw std::runtime_error("cannot open SQLite session storage");
        sqlite3_busy_timeout(handle,5000);
        run("PRAGMA journal_mode=WAL;");
        run("CREATE TABLE IF NOT EXISTS nq_web_sessions("
            "sid TEXT PRIMARY KEY NOT NULL, principal TEXT NOT NULL,"
            "csrf TEXT NOT NULL, session_values TEXT NOT NULL,"
            "updated_at INTEGER NOT NULL);");
    }
    ~NativeSqliteSessions(){if(handle)sqlite3_close(handle);}
    NativeSqliteSessions(const NativeSqliteSessions&)=delete;
    NativeSqliteSessions& operator=(const NativeSqliteSessions&)=delete;
    void run(const char* sql) {
        char* error=nullptr;
        if(sqlite3_exec(handle,sql,nullptr,nullptr,&error)!=SQLITE_OK) {
            if(error)sqlite3_free(error);
            throw std::runtime_error("SQLite session operation failed");
        }
    }
    static std::int64_t epoch() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    static std::string encode(const AppSession& session) {
        std::string out;
        for(const auto& [name,value]:session.values) {
            if(!validName(name)||name.size()>64||value.size()>512)
                throw std::runtime_error("invalid session value");
            out+=name+"="+hexEncode(value)+"\n";
            if(out.size()>16384)throw std::runtime_error("session exceeds 16 KiB");
        }
        return out;
    }
    static std::map<std::string,std::string> decode(const std::string& input) {
        if(input.size()>16384)throw std::runtime_error("corrupt or oversized session");
        std::map<std::string,std::string> values;
        std::istringstream lines(input);
        std::string item;
        while(std::getline(lines,item)) {
            const auto sep=item.find('=');
            if(sep==std::string::npos)throw std::runtime_error("invalid serialized session");
            const auto key=item.substr(0,sep);
            if(!validName(key)||key.size()>64||values.count(key))
                throw std::runtime_error("invalid serialized session key");
            auto value=hexDecode(item.substr(sep+1));
            if(value.size()>512)throw std::runtime_error("invalid serialized session value");
            values.emplace(key,std::move(value));
            if(values.size()>128)throw std::runtime_error("too many session values");
        }
        return values;
    }
    class Lease {
        NativeSqliteSessions& store;
        bool committed=false;
    public:
        explicit Lease(NativeSqliteSessions& sessions):store(sessions) {
            store.run("BEGIN IMMEDIATE;");
        }
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        ~Lease(){
            if(!committed) {
                // Failure-path rollback is required for cross-process replay protection.
                try{store.run("ROLLBACK;");}catch(...){}
            }
        }
        void commit(){store.run("COMMIT;");committed=true;}
    };
    bool load(const std::string& sid,AppSession& value,std::chrono::seconds ttl) {
        if(sid.empty())return false;
        sqlite3_stmt* stmt=nullptr;
        const char* sql="SELECT principal,csrf,session_values,updated_at "
            "FROM nq_web_sessions WHERE sid=?1";
        if(sqlite3_prepare_v2(handle,sql,-1,&stmt,nullptr)!=SQLITE_OK)
            throw std::runtime_error("cannot prepare SQLite session fetch");
        const auto cleanup=[&](){sqlite3_finalize(stmt);};
        sqlite3_bind_text(stmt,1,sid.c_str(),static_cast<int>(sid.size()),SQLITE_TRANSIENT);
        const int status=sqlite3_step(stmt);
        if(status==SQLITE_DONE){cleanup();return false;}
        if(status!=SQLITE_ROW){cleanup();throw std::runtime_error("SQLite session read failed");}
        const auto text=[&](int col)->std::string {
            const auto* raw=sqlite3_column_text(stmt,col);
            const auto size=sqlite3_column_bytes(stmt,col);
            if(!raw||size<0||size>16384)throw std::runtime_error("invalid SQLite session row");
            return std::string(reinterpret_cast<const char*>(raw),static_cast<std::size_t>(size));
        };
        try {
            const auto timestamp=sqlite3_column_int64(stmt,3);
            if(timestamp>epoch()||epoch()-timestamp>ttl.count()){
                cleanup();return false;
            }
            value.principal=text(0);
            value.csrf=text(1);
            if(value.csrf.size()!=48)throw std::runtime_error("invalid SQLite CSRF token");
            value.values=decode(text(2));
            value.seen=std::chrono::steady_clock::now();
        }catch(...){cleanup();throw;}
        cleanup();return true;
    }
    void save(const std::string& sid,const AppSession& session) {
        sqlite3_stmt* stmt=nullptr;
        const char* sql="INSERT INTO nq_web_sessions(sid,principal,csrf,session_values,updated_at) "
          "VALUES(?1,?2,?3,?4,?5) ON CONFLICT(sid) DO UPDATE SET "
          "principal=excluded.principal,csrf=excluded.csrf,"
          "session_values=excluded.session_values,updated_at=excluded.updated_at";
        if(sqlite3_prepare_v2(handle,sql,-1,&stmt,nullptr)!=SQLITE_OK)
            throw std::runtime_error("cannot prepare SQLite session write");
        const auto serialized=encode(session);
        sqlite3_bind_text(stmt,1,sid.c_str(),static_cast<int>(sid.size()),SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,2,session.principal.c_str(),static_cast<int>(session.principal.size()),SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,3,session.csrf.c_str(),static_cast<int>(session.csrf.size()),SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,4,serialized.c_str(),static_cast<int>(serialized.size()),SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt,5,epoch());
        const auto status=sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if(status!=SQLITE_DONE)throw std::runtime_error("SQLite session write failed");
    }
};
#endif
