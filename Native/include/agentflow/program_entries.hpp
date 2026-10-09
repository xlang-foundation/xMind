#pragma once
// Native entry handlers shared by the unified product and transitional launchers.
// These do not transfer backend ownership to a client.
int run_server(int argc,char** argv);
int cli_main(int argc,char** argv);
int run_schema_worker(int argc,char** argv);
#if defined(_WIN32)
int run_admin(int argc,char** argv);
#endif
