// SPDX-License-Identifier: GPL-3.0-only
// Native web DB boundary. All engines are opt-in; NoeSQL owns .noedb.
// No implicit NoqeriDB/NoeSQL format conversion and no SQL source interpolation.
#pragma once
struct DbParameter {
    std::string name, value;
};
std::string dbJsonQuote(const std::string& value) {
    std::string out="\"";
    static const char hex[]="0123456789abcdef";
    for(unsigned char c:value) {
        switch(c) {
        case '"':out+="\\\"";break;
        case '\\':out+="\\\\";break;
        case '\n':out+="\\n";break;
        case '\r':out+="\\r";break;
        case '\t':out+="\\t";break;
        default:
            if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
            else out+=static_cast<char>(c);
        }
    }
    return out+"\"";
}
std::string dbRowsJson(const std::vector<std::string>& fields,
                       const std::vector<std::vector<std::string>>& rows) {
    std::string out="[";
    for(const auto& row:rows) {
        if(out.size()>65536)throw std::runtime_error("database output exceeds limit");
        if(out!="[")out+=",";
        out+="{";
        for(std::size_t col=0;col<row.size();++col) {
            if(col)out+=",";
            out+=dbJsonQuote(fields.at(col))+":"+dbJsonQuote(row[col]);
        }
        out+="}";
    }
    return out+"]";
}
// Convert known native numeric and boolean types to valid JSON primitives;
// unrecognized database types remain escaped JSON strings.
std::string dbTypedPrimitive(const std::string& value,unsigned type,bool postgres) {
    if(postgres && type==16) {
        if(value=="t")return "true";
        if(value=="f")return "false";
        throw std::runtime_error("invalid PostgreSQL boolean value");
    }
    if(postgres && (type==20 || type==21 || type==23)) {
        if(value.empty())throw std::runtime_error("invalid PostgreSQL integer value");
        std::size_t at=value[0]=='-'?1:0;
        if(at==value.size())throw std::runtime_error("invalid PostgreSQL integer value");
        for(;at<value.size();++at)if(value[at]<'0'||value[at]>'9')
            throw std::runtime_error("invalid PostgreSQL integer value");
        return value;
    }
    if(postgres && (type==700 || type==701 || type==1700)) {
        if(value.size()>64)throw std::runtime_error("PostgreSQL floating value too long");
        std::size_t consumed=0;
        try {
            const auto number=std::stod(value,&consumed);
            if(consumed==value.size() && std::isfinite(number))return value;
        } catch(const std::exception&) {}
        throw std::runtime_error("invalid PostgreSQL numeric JSON value");
    }
    return dbJsonQuote(value);
}
std::string dbTypedRowsJson(const std::vector<std::string>& columns,
                            const std::vector<std::vector<std::string>>& jsonValues) {
    std::string out="[";
    for(const auto& row:jsonValues) {
        if(out!="[")out+=",";
        out+="{";
        for(std::size_t i=0;i<row.size();++i) {
            if(i)out+=",";
            out+=dbJsonQuote(columns.at(i))+":"+row[i];
        }
        out+="}";
        if(out.size()>65536)throw std::runtime_error("database JSON exceeds 64 KiB");
    }
    return out+"]";
}
void verifyDbConfig(const DbConfiguration& config) {
    if(config.engine!="noesql"&&config.engine!="sqlite"&&config.engine!="postgresql"&&
       config.engine!="duckdb"&&config.engine!="mysql")
        throw std::runtime_error("unsupported database engine; choose noesql, sqlite, postgresql, duckdb, mysql");
    if(config.engine=="noesql"||config.engine=="sqlite"||config.engine=="duckdb") {
        if(config.path.empty()||config.path.size()>2048)
            throw std::runtime_error("database path is required");
        if(config.engine=="noesql"&&fs::path(config.path).extension()!=".noedb")
            throw std::runtime_error("NoeSQL requires canonical .noedb file; no legacy .nqd/.nqdb fallback");
    } else {
        if(config.dsnEnvironment.empty()||config.dsnEnvironment.size()>128||
           !validName(config.dsnEnvironment))
            throw std::runtime_error("database connection must name an environment variable");
    }
}
#if !defined(_WIN32)
std::string runNoeSqlCommand(const DbConfiguration& db,const std::string& command,
                             const std::string& statement,const std::vector<DbParameter>& params) {
    const char* env=std::getenv(db.binaryEnvironment.c_str());
    if(!env||!*env)throw std::runtime_error("NoeSQL native executable missing: set NOESQL_BIN");
    if(statement.size()>65536||params.size()>32)throw std::runtime_error("NoeSQL request exceeds limit");
    int descriptors[2];
    if(::pipe(descriptors)<0)throw std::runtime_error("NoeSQL pipe failure");
    const pid_t pid=::fork();
    if(pid<0) {
        ::close(descriptors[0]);::close(descriptors[1]);
        throw std::runtime_error("NoeSQL fork failed");
    }
    if(pid==0) {
        ::close(descriptors[0]);
        if(::dup2(descriptors[1],STDOUT_FILENO)<0)_exit(127);
        ::close(descriptors[1]);
        const int devnull=::open("/dev/null",O_WRONLY);
        if(devnull>=0) {::dup2(devnull,STDERR_FILENO);::close(devnull);}
        std::vector<std::string> words={env,command,db.path,statement};
        for(const auto& item:params) {
            if(!validName(item.name)||item.value.size()>4096)_exit(127);
            words.push_back(item.name+":text:"+item.value);
        }
        std::vector<char*> argv;
        for(auto& word:words) argv.push_back(const_cast<char*>(word.c_str()));
        argv.push_back(nullptr);
        ::execvp(env,argv.data());
        _exit(127);
    }
    ::close(descriptors[1]);
    std::string output;char buffer[2048];ssize_t count;
    while((count=::read(descriptors[0],buffer,sizeof(buffer)))>0) {
        if(output.size()+static_cast<std::size_t>(count)>65536) {
            ::close(descriptors[0]);
            ::kill(pid,SIGKILL);
            int status=0;::waitpid(pid,&status,0);
            throw std::runtime_error("NoeSQL output exceeds limit");
        }
        output.append(buffer,static_cast<std::size_t>(count));
    }
    ::close(descriptors[0]);
    int status=0;
    if(::waitpid(pid,&status,0)<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0)
        throw std::runtime_error("NoeSQL native engine rejected statement or storage operation");
    while(!output.empty()&&(output.back()=='\n'||output.back()=='\r'))output.pop_back();
    return output;
}
#endif
struct SqlPrepared {
    std::string source;
    std::vector<std::string> values;
};
// Named :params are replaced only outside SQL quoted strings/comments.
// Callers author SQL in trusted .noqeri source, never in submitted HTTP fields.
SqlPrepared preparePortableSql(const std::string& source,
                               const std::vector<DbParameter>& params,const std::string& engine) {
    if(source.empty()||source.size()>16384||params.size()>32)
        throw std::runtime_error("SQL statement exceeds limit");
    std::map<std::string,std::string> binds;
    for(const auto& item:params) {
        if(!validName(item.name)||!binds.emplace(item.name,item.value).second)
            throw std::runtime_error("invalid or duplicate SQL parameter");
    }
    std::set<std::string> used;
    SqlPrepared result;bool quotedSingle=false,quotedDouble=false;
    for(std::size_t i=0;i<source.size();++i) {
        const char c=source[i];
        if(c=='\0'||c==';')throw std::runtime_error("SQL adapter permits one statement without semicolons");
        if(c=='\''&&!quotedDouble) {
            result.source+=c;
            if(quotedSingle&&i+1<source.size()&&source[i+1]=='\'')result.source+=source[++i];
            else quotedSingle=!quotedSingle;
            continue;
        }
        if(c=='"'&&!quotedSingle) {
            result.source+=c;
            if(quotedDouble&&i+1<source.size()&&source[i+1]=='"')result.source+=source[++i];
            else quotedDouble=!quotedDouble;
            continue;
        }
        if(!quotedSingle&&!quotedDouble&&c=='-'&&i+1<source.size()&&source[i+1]=='-')
            throw std::runtime_error("SQL comments unsupported in portable statements");
        if(!quotedSingle&&!quotedDouble&&c=='/'&&i+1<source.size()&&source[i+1]=='*')
            throw std::runtime_error("SQL comments unsupported in portable statements");
        if(!quotedSingle&&!quotedDouble&&c==':'&&i+1<source.size()&&wordStart(source[i+1])&&
           !(i>0&&source[i-1]==':')) {
            const auto start=++i;
            while(i<source.size()&&wordContinue(source[i]))++i;
            const auto name=source.substr(start,i-start);
            --i;
            const auto value=binds.find(name);
            if(value==binds.end())throw std::runtime_error("missing SQL bind: "+name);
            used.insert(name);
            result.values.push_back(value->second);
            result.source+=engine=="postgresql"?"$"+std::to_string(result.values.size()):"?";
        }else result.source+=c;
    }
    if(quotedSingle||quotedDouble||used.size()!=binds.size())
        throw std::runtime_error("invalid SQL string or unused SQL bind");
    return result;
}
#ifdef NOQERI_WEB_SQLITE
std::string querySqlite(const DbConfiguration& config,const SqlPrepared& sql) {
    sqlite3* handle=nullptr;
    if(sqlite3_open_v2(config.path.c_str(),&handle,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK) {
        if(handle)sqlite3_close(handle);
        throw std::runtime_error("SQLite open failed");
    }
    struct Close {sqlite3* h;~Close(){sqlite3_close(h);}} close{handle};
    sqlite3_busy_timeout(handle,3000);
    sqlite3_exec(handle,"PRAGMA foreign_keys=ON",nullptr,nullptr,nullptr);
    sqlite3_stmt* statement=nullptr;const char* rest=nullptr;
    if(sqlite3_prepare_v2(handle,sql.source.c_str(),-1,&statement,&rest)!=SQLITE_OK)
        throw std::runtime_error("SQLite prepare rejected statement");
    if(!statement)throw std::runtime_error("SQLite empty statement");
    struct Finish {sqlite3_stmt* h;~Finish(){sqlite3_finalize(h);}} finish{statement};
    if(rest&&!std::all_of(rest,rest+std::strlen(rest),[](unsigned char c){return std::isspace(c)!=0;}))
        throw std::runtime_error("SQLite disallows multiple statements");
    if(sqlite3_bind_parameter_count(statement)!=static_cast<int>(sql.values.size()))
        throw std::runtime_error("SQLite parameter count mismatch");
    for(std::size_t i=0;i<sql.values.size();++i)
        if(sqlite3_bind_text(statement,static_cast<int>(i+1),sql.values[i].c_str(),
            static_cast<int>(sql.values[i].size()),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("SQLite parameter bind failed");
    std::vector<std::string> columns;
    const int count=sqlite3_column_count(statement);
    if(count>64)throw std::runtime_error("SQLite too many output columns");
    for(int i=0;i<count;++i)columns.emplace_back(sqlite3_column_name(statement,i));
    std::vector<std::vector<std::string>> rows;
    for(;;) {
        const int rc=sqlite3_step(statement);
        if(rc==SQLITE_DONE)break;
        if(rc!=SQLITE_ROW)throw std::runtime_error("SQLite execution error");
        if(rows.size()>=256)throw std::runtime_error("SQLite row limit exceeded");
        std::vector<std::string> row;
        for(int i=0;i<count;++i) {
            const auto* bytes=sqlite3_column_text(statement,i);
            const int size=sqlite3_column_bytes(statement,i);
            if(size>4096)throw std::runtime_error("SQLite column too large");
            const auto type=sqlite3_column_type(statement,i);
            if(type==SQLITE_NULL)row.emplace_back("null");
            else if(type==SQLITE_INTEGER)
                row.emplace_back(std::to_string(sqlite3_column_int64(statement,i)));
            else if(type==SQLITE_FLOAT) {
                const double value=sqlite3_column_double(statement,i);
                if(!std::isfinite(value))throw std::runtime_error("nonfinite SQLite value");
                std::ostringstream num;
                num<<std::setprecision(17)<<value;
                row.push_back(num.str());
            } else if(type==SQLITE_TEXT) {
                row.push_back(dbJsonQuote(bytes?
                    std::string(reinterpret_cast<const char*>(bytes),size):""));
            } else throw std::runtime_error("binary SQLite results require explicit conversion");

        }
        rows.push_back(std::move(row));
    }
    return count?dbTypedRowsJson(columns,rows):"affected="+std::to_string(sqlite3_changes(handle));
}
#endif
#ifdef NOQERI_WEB_POSTGRES
std::string queryPostgres(const DbConfiguration& config,const SqlPrepared& sql) {
    const char* dsn=std::getenv(config.dsnEnvironment.c_str());
    if(!dsn||!*dsn)throw std::runtime_error("PostgreSQL environment DSN missing");
    PGconn* conn=PQconnectdb(dsn);
    if(!conn)throw std::runtime_error("PostgreSQL connection allocation failed");
    struct Close{PGconn* c;~Close(){PQfinish(c);}} close{conn};
    if(PQstatus(conn)!=CONNECTION_OK)throw std::runtime_error("PostgreSQL connection failed");
    std::vector<const char*> values;for(const auto& v:sql.values)values.push_back(v.c_str());
    PGresult* result=PQexecParams(conn,sql.source.c_str(),static_cast<int>(values.size()),
        nullptr,values.empty()?nullptr:values.data(),nullptr,nullptr,0);
    if(!result)throw std::runtime_error("PostgreSQL query failed");
    struct Clear{PGresult* r;~Clear(){PQclear(r);}} clear{result};
    const auto status=PQresultStatus(result);
    if(status!=PGRES_TUPLES_OK&&status!=PGRES_COMMAND_OK)
        throw std::runtime_error("PostgreSQL rejected prepared query");
    if(status==PGRES_COMMAND_OK)return "affected="+std::string(PQcmdTuples(result));
    const int columns=PQnfields(result),rows=PQntuples(result);
    if(columns>64||rows>256)throw std::runtime_error("PostgreSQL result too large");
    std::vector<std::string> headers;
    for(int i=0;i<columns;++i)headers.emplace_back(PQfname(result,i));
    std::vector<std::vector<std::string>> valuesRows;
    for(int row=0;row<rows;++row) {
        std::vector<std::string> valuesRow;
        for(int col=0;col<columns;++col) {
            const auto len=PQgetlength(result,row,col);
            if(len>4096)throw std::runtime_error("PostgreSQL cell too large");
            if(PQgetisnull(result,row,col))valuesRow.emplace_back("null");
            else valuesRow.emplace_back(dbTypedPrimitive(std::string(PQgetvalue(result,row,col),len),
                                                       PQftype(result,col),true));
        }
        valuesRows.push_back(std::move(valuesRow));
    }
    return dbTypedRowsJson(headers,valuesRows);
}
#endif
#ifdef NOQERI_WEB_DUCKDB
std::string queryDuckdb(const DbConfiguration& config,const SqlPrepared& sql) {
    duckdb_database db=nullptr;
    if(duckdb_open(config.path.c_str(),&db)!=DuckDBSuccess)
        throw std::runtime_error("DuckDB open failed");
    struct Close {duckdb_database& d;~Close(){duckdb_close(&d);}} close{db};
    duckdb_connection conn=nullptr;
    if(duckdb_connect(db,&conn)!=DuckDBSuccess)throw std::runtime_error("DuckDB connect failed");
    struct Disconnect {duckdb_connection& c;~Disconnect(){duckdb_disconnect(&c);}} disc{conn};
    duckdb_prepared_statement prep=nullptr;
    if(duckdb_prepare(conn,sql.source.c_str(),&prep)!=DuckDBSuccess)
        throw std::runtime_error("DuckDB prepare failed");
    struct Destroy {duckdb_prepared_statement& p;~Destroy(){duckdb_destroy_prepare(&p);}} destroy{prep};
    if(duckdb_nparams(prep)!=sql.values.size())throw std::runtime_error("DuckDB parameter count mismatch");
    for(std::size_t i=0;i<sql.values.size();++i)
        if(duckdb_bind_varchar(prep,i+1,sql.values[i].c_str())!=DuckDBSuccess)
            throw std::runtime_error("DuckDB bind failed");
    duckdb_result output{};
    if(duckdb_execute_prepared(prep,&output)!=DuckDBSuccess) {
        duckdb_destroy_result(&output);
        throw std::runtime_error("DuckDB execution failed");
    }
    struct FreeResult {duckdb_result& result;~FreeResult(){duckdb_destroy_result(&result);}} clear{output};
    const auto cols=duckdb_column_count(&output),rows=duckdb_row_count(&output);
    if(cols>64||rows>256)throw std::runtime_error("DuckDB result too large");
    std::vector<std::string> headers;std::vector<std::vector<std::string>> valuesRows;
    for(duckdb_idx_t i=0;i<cols;++i)headers.emplace_back(duckdb_column_name(&output,i));
    for(duckdb_idx_t row=0;row<rows;++row) {
        std::vector<std::string> valuesRow;
        for(duckdb_idx_t col=0;col<cols;++col) {
            char* value=duckdb_value_varchar(&output,col,row);
            std::string text=value?value:"";
            if(value)duckdb_free(value);
            if(text.size()>4096)throw std::runtime_error("DuckDB cell too large");
            valuesRow.push_back(value ? dbJsonQuote(text) : "null");
        }
        valuesRows.push_back(std::move(valuesRow));
    }
    return cols?dbTypedRowsJson(headers,valuesRows):"affected=0";
}
#endif
#ifdef NOQERI_WEB_MYSQL
std::string queryMysql(const DbConfiguration& config,const SqlPrepared& sql) {
    const char* host=std::getenv("NOQERI_MYSQL_HOST");
    const char* user=std::getenv("NOQERI_MYSQL_USER");
    const char* pass=std::getenv("NOQERI_MYSQL_PASSWORD");
    const char* name=std::getenv(config.dsnEnvironment.c_str());
    if(!host||!user||!pass||!name)throw std::runtime_error("MySQL connection environment missing");
    MYSQL* raw=mysql_init(nullptr);
    if(!raw)throw std::runtime_error("MySQL initialization failed");
    struct Close{MYSQL* c;~Close(){mysql_close(c);}} close{raw};
    if(!mysql_real_connect(raw,host,user,pass,name,0,nullptr,0))
        throw std::runtime_error("MySQL connection failed");
    MYSQL_STMT* stmt=mysql_stmt_init(raw);
    if(!stmt)throw std::runtime_error("MySQL statement allocation failed");
    struct Free{MYSQL_STMT* s;~Free(){mysql_stmt_close(s);}} finish{stmt};
    if(mysql_stmt_prepare(stmt,sql.source.c_str(),static_cast<unsigned long>(sql.source.size()))!=0)
        throw std::runtime_error("MySQL prepare failed");
    if(mysql_stmt_param_count(stmt)!=sql.values.size())
        throw std::runtime_error("MySQL parameter count mismatch");
    std::vector<MYSQL_BIND> binds(sql.values.size());
    std::vector<unsigned long> lengths(sql.values.size());
    for(std::size_t i=0;i<sql.values.size();++i) {
        lengths[i]=static_cast<unsigned long>(sql.values[i].size());
        binds[i].buffer_type=MYSQL_TYPE_STRING;
        binds[i].buffer=const_cast<char*>(sql.values[i].c_str());
        binds[i].buffer_length=lengths[i];
        binds[i].length=&lengths[i];
    }
    if(!binds.empty()&&mysql_stmt_bind_param(stmt,binds.data())!=0)
        throw std::runtime_error("MySQL bind failed");
    if(mysql_stmt_execute(stmt)!=0)throw std::runtime_error("MySQL execute failed");
    MYSQL_RES* metadata=mysql_stmt_result_metadata(stmt);
    if(!metadata)return "affected="+std::to_string(mysql_stmt_affected_rows(stmt));
    struct FreeMeta{MYSQL_RES* r;~FreeMeta(){mysql_free_result(r);}} clear{metadata};
    const auto count=mysql_num_fields(metadata);
    if(count>64)throw std::runtime_error("MySQL too many columns");
    MYSQL_FIELD* meta=mysql_fetch_fields(metadata);
    std::vector<std::string> headers;
    for(unsigned i=0;i<count;++i)headers.emplace_back(meta[i].name);
    std::vector<std::array<char,4097>> buffers(count);
    std::vector<MYSQL_BIND> results(count);
    std::vector<unsigned long> sizes(count);
    using NullFlag=std::remove_pointer_t<decltype(MYSQL_BIND{}.is_null)>;
    std::unique_ptr<NullFlag[]> nulls(new NullFlag[count]());
    for(unsigned i=0;i<count;++i) {
        results[i].buffer_type=MYSQL_TYPE_STRING;
        results[i].buffer=buffers[i].data();
        results[i].buffer_length=4096;
        results[i].length=&sizes[i];results[i].is_null=&nulls[i];
    }
    if(mysql_stmt_bind_result(stmt,results.data())!=0 ||
       mysql_stmt_store_result(stmt)!=0)throw std::runtime_error("MySQL result preparation failed");
    std::vector<std::vector<std::string>> rows;
    for(;;) {
        const int status=mysql_stmt_fetch(stmt);
        if(status==MYSQL_NO_DATA)break;
        if(status!=0)throw std::runtime_error("MySQL result truncated or fetch failed");
        if(rows.size()>=256)throw std::runtime_error("MySQL row count exceeded");
        std::vector<std::string> row;
        for(unsigned i=0;i<count;++i) {
            if(sizes[i]>4096)throw std::runtime_error("MySQL cell exceeds size limit");
            row.emplace_back(nulls[i]?"null":dbJsonQuote(std::string(buffers[i].data(),sizes[i])));
        }
        rows.push_back(std::move(row));
    }
    return dbTypedRowsJson(headers,rows);
}
#endif

#if !defined(_WIN32)
std::string invokeNoeSqlSessionV2(const std::string& library,
                                  const std::string& database,const std::string& source,
                                  const std::vector<DbParameter>& params) {
    if(library.empty()||library[0]!='/'||library.find('\0')!=std::string::npos)
        throw std::runtime_error("NoeSQL v2 native provider must be an absolute shared-library path");
    if(source.empty()||source.size()>65536||params.size()>32)
        throw std::runtime_error("NoeSQL native request exceeds bound");
    // Provider module stays loaded for this process. The Rust callback table
    // owns connection and prepared statement lifetimes; never copy its memory.
    struct Module { void* handle=nullptr; const NqNoeSqlSessionV2* api=nullptr; };
    // Provider callback tables and the current adapter profile are not
    // guaranteed thread safe. Serialize loading and session calls until the
    // canonical provider exposes a concurrency contract.
    static std::mutex providerMutex;
    std::lock_guard<std::mutex> lock(providerMutex);
    static std::map<std::string,Module> loaded;
    auto found=loaded.find(library);
    if(found==loaded.end()) {
        void* handle=::dlopen(library.c_str(),RTLD_NOW|RTLD_LOCAL);
        if(!handle)throw std::runtime_error("NoeSQL v2 native provider could not be loaded");
        auto getter=reinterpret_cast<NqNoeSqlSessionGetter>(::dlsym(handle,"noesql_session_v2_get"));
        if(!getter) {::dlclose(handle);throw std::runtime_error("NoeSQL v2 getter export absent");}
        const auto* api=getter();
        if(!api||api->abi_version!=2||!api->open||!api->close||!api->prepare||
           !api->release||!api->execute||!api->query||!api->begin||
           !api->commit||!api->rollback) {
            ::dlclose(handle);
            throw std::runtime_error("NoeSQL v2 provider has an invalid callback table");
        }
        found=loaded.emplace(library,Module{handle,api}).first;
    }
    const auto* api=found->second.api;
    void* connection=nullptr;
    if(api->open(api->context,reinterpret_cast<const std::uint8_t*>(database.data()),
                 database.size(),&connection)!=0||!connection)
        throw std::runtime_error("NoeSQL v2 open failed");
    struct Close {
        const NqNoeSqlSessionV2* api;void* connection;
        ~Close(){api->close(api->context,connection);}
    } close{api,connection};
    void* prepared=nullptr;
    if(api->prepare(api->context,connection,
                   reinterpret_cast<const std::uint8_t*>(source.data()),source.size(),
                   &prepared)!=0||!prepared)
        throw std::runtime_error("NoeSQL v2 prepare failed");
    struct Release {
        const NqNoeSqlSessionV2* api;void* connection;void* statement;
        ~Release(){api->release(api->context,connection,statement);}
    } release{api,connection,prepared};
    std::string json="{";
    std::set<std::string> seen;
    for(const auto& param:params) {
        if(!validName(param.name)||!seen.insert(param.name).second||param.value.size()>4096)
            throw std::runtime_error("NoeSQL v2 invalid bind");
        if(json!="{")json+=",";
        json+=dbJsonQuote(param.name)+":"+dbJsonQuote(param.value);
    }
    json+="}";
    if(json.size()>32768)throw std::runtime_error("NoeSQL v2 binding JSON too large");
    const bool read=source.find(" get ")!=std::string::npos;
    if(!read&&api->begin(api->context,connection)!=0)
        throw std::runtime_error("NoeSQL v2 transaction begin failed");
    std::array<std::uint8_t,65536> output{};
    std::size_t written=0;
    const auto callback=read?api->query:api->execute;
    const int code=callback(api->context,connection,prepared,
        reinterpret_cast<const std::uint8_t*>(json.data()),json.size(),
        output.data(),output.size(),&written);
    if(code!=0||written>output.size()) {
        if(!read)api->rollback(api->context,connection);
        throw std::runtime_error("NoeSQL v2 execution failed");
    }
    if(!read&&api->commit(api->context,connection)!=0) {
        api->rollback(api->context,connection);
        throw std::runtime_error("NoeSQL v2 commit failed");
    }
    return std::string(reinterpret_cast<const char*>(output.data()),written);
}
#endif
std::string executeDatabase(const DbConfiguration& db,const std::string& source,
                            const std::vector<DbParameter>& params) {
    verifyDbConfig(db);
    if(db.engine=="noesql") {
#if !defined(_WIN32)
        // A configured native session provider takes precedence and fails
        // closed; the legacy subprocess adapter remains only when no provider
        // is configured. The real NoeSQL v2 provider must be built separately.
        if(const char* native=std::getenv("NOESQL_NATIVE_PROVIDER"))
            return invokeNoeSqlSessionV2(native,db.path,source,params);
        return runNoeSqlCommand(db,"exec",source,params);
#else
        throw std::runtime_error("NoeSQL CLI adapter requires POSIX host");
#endif
    }
    const auto prepared=preparePortableSql(source,params,db.engine);
    if(db.engine=="sqlite") {
#ifdef NOQERI_WEB_SQLITE
        return querySqlite(db,prepared);
#else
        throw std::runtime_error("SQLite adapter not built: install SQLite development package");
#endif
    }
    if(db.engine=="postgresql") {
#ifdef NOQERI_WEB_POSTGRES
        return queryPostgres(db,prepared);
#else
        throw std::runtime_error("PostgreSQL adapter not built: install libpq development package");
#endif
    }
    if(db.engine=="duckdb") {
#ifdef NOQERI_WEB_DUCKDB
        return queryDuckdb(db,prepared);
#else
        throw std::runtime_error("DuckDB adapter not built: install DuckDB C client library");
#endif
    }
    if(db.engine=="mysql") {
#ifdef NOQERI_WEB_MYSQL
        return queryMysql(db,prepared);
#else
        throw std::runtime_error("MySQL adapter not built: install MariaDB/MySQL C client library");
#endif
    }
    throw std::runtime_error("invalid engine");
}
