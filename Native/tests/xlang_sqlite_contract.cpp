#include "agentflow/xlang_sqlite.hpp"
#include <iostream>
#include <thread>

using namespace agentflow;
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    if(argc!=3) { std::cerr<<"Expected native-package and stdlib-source directories\n"; return 2; }
    try {
        XlangSqlite database(":memory:",{argv[1],argv[2]});
        database.execute("CREATE TABLE test(id INTEGER PRIMARY KEY, data BLOB, text TEXT, count INTEGER, number REAL, empty TEXT)");
        const SqlBytes blob{0,1,255,0}; const std::string text("hello\0world",11);
        auto insert=database.execute("INSERT INTO test VALUES(?,?,?,?,?,?)",{std::int64_t{1},blob,text,std::int64_t{42},2.5,nullptr});
        require(insert.affected_rows==1,"Insert must report one affected row");
        require(insert.last_insert_id==1,"Insert ID must be retained");
        const auto result=database.execute("SELECT data,text,count,number,empty FROM test WHERE id=?",{std::int64_t{1}});
        require(result.rows.size()==1 && result.rows[0].size()==5,"Query shape incorrect");
        require(std::get<SqlBytes>(result.rows[0][0])==blob,"Binary blob round trip failed");
        require(std::get<std::string>(result.rows[0][1])==text,"Embedded-null text round trip failed");
        require(std::get<std::int64_t>(result.rows[0][2])==42,"Integer round trip failed");
        require(std::get<double>(result.rows[0][3])==2.5,"Real round trip failed");
        require(std::holds_alternative<std::nullptr_t>(result.rows[0][4]),"NULL round trip failed");
        database.begin();
        database.execute("UPDATE test SET text=? WHERE id=?",{std::string("changed"),std::int64_t{1}});
        database.rollback();
        require(std::get<std::string>(database.execute("SELECT text FROM test").rows[0][0])==text,"Rollback failed");
        bool rejected=false;
        std::thread wrong([&]{ try {database.execute("SELECT 1");} catch(const std::logic_error&) {rejected=true;} });
        wrong.join(); require(rejected,"Wrong thread must be rejected before runtime access");
        std::cout<<"Embedded xlang3 SQLite contracts passed\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
