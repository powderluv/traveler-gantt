/*
 * Copyright (c) 2014 Cesanta Software Limited
 * All rights reserved
 */

#include <cstdlib>
#include <string>
#include <map>
#include <utility>
#include <iostream>
#include "trace.h"
#include "importfunctor.h"
#include "external/mongoose.h"

static const char *s_http_port = "8000";
static struct mg_serve_http_opts s_http_server_opts;

Trace * trace = NULL;

static std::map<int, int> * memo = new std::map<int, int>();

static int fib(int f) {
    if (f < 0) {
        return -1;
    }
    if (memo->find(f) != memo->end()) {
        return memo->find(f)->second;
    }

    memo->insert(std::pair<int, int>(f, fib(f - 1) + fib(f - 2)));
    return memo->find(f)->second;
}

static std::string check_memo(int f) {
    return (memo->find(f) != memo->end()) ? "Yes" : "No";
}


static void handle_fib_call(struct mg_connection *nc, struct http_message *hm) {
  char f[100];
  double result;
  std::string was_memo;

  /* Get form variables */
  mg_get_http_var(&hm->body, "f", f, sizeof(f));

  /* Send headers */
  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");

  /* Compute the result and send it back as a JSON object */
  was_memo = check_memo(strtod(f, NULL));
  result = fib(strtod(f, NULL));
  //mg_printf_http_chunk(nc, "{ \"result\": %lf }", result);
  printf("%s\n", was_memo.c_str());
  mg_printf_http_chunk(nc, "{ \"result\": %lf, \"memo\": \"%s\" }", result, was_memo.c_str());
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
  struct http_message *hm = (struct http_message *) ev_data;

  switch (ev) {
    case MG_EV_HTTP_REQUEST:
      if (mg_vcmp(&hm->uri, "/api/v1/fib") == 0) {
        handle_fib_call(nc, hm); /* Handle RESTful call */
      } else if (mg_vcmp(&hm->uri, "/printcontent") == 0) {
        char buf[100] = {0};
        memcpy(buf, hm->body.p,
               sizeof(buf) - 1 < hm->body.len ? sizeof(buf) - 1 : hm->body.len);
        printf("%s\n", buf);
      } else {
        mg_serve_http(nc, hm, s_http_server_opts); /* Serve static content */
      }
      break;
    default:
      break;
  }
}

static void setTrace(std::string dataFileName) {
    trace = NULL;
    if (dataFileName.length() == 0) {
        std::cout << "No trace file given." << std::endl;
    }

    ImportFunctor * importWorker = new ImportFunctor();

    if (dataFileName.compare(dataFileName.length() - 3, 3, "otf"))
    {
        trace = importWorker->doImportOTF(dataFileName);
    }
    else if (dataFileName.compare(dataFileName.length() - 3, 3, "otf2"))
    {
        trace = importWorker->doImportOTF2(dataFileName);
    }
    else
    {
        std::cout << "Unrecognized trace format!" << std::endl;
    }
    delete importWorker;
}

int main(int argc, char *argv[]) {
  struct mg_mgr mgr;
  struct mg_connection *nc;
  struct mg_bind_opts bind_opts;
  int i;
  char *cp;
  const char *err_str;
#if MG_ENABLE_SSL
  const char *ssl_cert = NULL;
#endif

  (*memo)[0] = 0;
  (*memo)[1] = 1;

  mg_mgr_init(&mgr, NULL);

  /* Use current binary directory as document root */
  if (argc > 0 && ((cp = strrchr(argv[0], DIRSEP)) != NULL)) {
    *cp = '\0';
    s_http_server_opts.document_root = argv[0];
  }

  std::string filename = "";
  /* Process command line options to customize HTTP server */
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
      mgr.hexdump_file = argv[++i];
    } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
      s_http_server_opts.document_root = argv[++i];
    } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
      s_http_port = argv[++i];
    } else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
      s_http_server_opts.auth_domain = argv[++i];
#if MG_ENABLE_JAVASCRIPT
    } else if (strcmp(argv[i], "-j") == 0 && i + 1 < argc) {
      const char *init_file = argv[++i];
      mg_enable_javascript(&mgr, v7_create(), init_file);
#endif
    } else if (strcmp(argv[i], "-P") == 0 && i + 1 < argc) {
      s_http_server_opts.global_auth_file = argv[++i];
    } else if (strcmp(argv[i], "-A") == 0 && i + 1 < argc) {
      s_http_server_opts.per_directory_auth_file = argv[++i];
    } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
      s_http_server_opts.url_rewrites = argv[++i];
#if MG_ENABLE_HTTP_CGI
    } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
      s_http_server_opts.cgi_interpreter = argv[++i];
#endif
#if MG_ENABLE_SSL
    } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
      ssl_cert = argv[++i];
#endif
    } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
        filename = argv[++i];
        setTrace(filename);
    } else {
      fprintf(stderr, "Unknown option: [%s]\n", argv[i]);
      exit(1);
    }
  }

  /* Set HTTP server options */
  memset(&bind_opts, 0, sizeof(bind_opts));
  bind_opts.error_string = &err_str;
#if MG_ENABLE_SSL
  if (ssl_cert != NULL) {
    bind_opts.ssl_cert = ssl_cert;
  }
#endif
  nc = mg_bind_opt(&mgr, s_http_port, ev_handler, bind_opts);
  if (nc == NULL) {
    fprintf(stderr, "Error starting server on port %s: %s\n", s_http_port,
            *bind_opts.error_string);
    exit(1);
  }

  mg_set_protocol_http_websocket(nc);
  s_http_server_opts.enable_directory_listing = "yes";

  printf("Starting RESTful server on port %s, serving %s\n", s_http_port,
         s_http_server_opts.document_root);
  for (;;) {
    mg_mgr_poll(&mgr, 1000);
  }
  mg_mgr_free(&mgr);

  return 0;
}
