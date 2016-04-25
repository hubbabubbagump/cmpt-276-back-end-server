/*
 Push Server code for CMPT 276, Spring 2016.
 */

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <cpprest/http_listener.h>
#include <cpprest/json.h>

#include <was/common.h>
#include <was/table.h>

#include "TableCache.h"
#include "make_unique.h"
#include "ClientUtils.h"

using azure::storage::cloud_storage_account;
using azure::storage::storage_credentials;
using azure::storage::storage_exception;
using azure::storage::cloud_table;
using azure::storage::cloud_table_client;
using azure::storage::edm_type;
using azure::storage::entity_property;
using azure::storage::table_entity;
using azure::storage::table_operation;
using azure::storage::table_query;
using azure::storage::table_query_iterator;
using azure::storage::table_result;

using pplx::extensibility::critical_section_t;
using pplx::extensibility::scoped_critical_section_t;

using std::cin;
using std::cout;
using std::endl;
using std::getline;
using std::make_pair;
using std::pair;
using std::string;
using std::unordered_map;
using std::vector;

using web::http::http_headers;
using web::http::http_request;
using web::http::methods;
using web::http::status_code;
using web::http::status_codes;
using web::http::uri;

using web::json::value;

using web::http::experimental::listener::http_listener;

using prop_str_vals_t = vector<pair<string,string>>;

constexpr const char* def_url = "http://localhost:34574";
const string addr {"http://localhost:34568/"};

const string data_table_name {"DataTable"};
const string read_entity_admin {"ReadEntityAdmin"};
const string update_entity_admin {"UpdateEntityAdmin"};
const string push_status {"PushStatus"};

unordered_map<string,string> get_json_body(http_request message) {  
  unordered_map<string,string> results {};
  const http_headers& headers {message.headers()};
  auto content_type (headers.find("Content-Type"));
  if (content_type == headers.end() ||
      content_type->second != "application/json")
    return results;

  value json{};
  message.extract_json(true)
    .then([&json](value v) -> bool
    {
            json = v;
      return true;
    })
    .wait();

  if (json.is_object()) {
    for (const auto& v : json.as_object()) {
      if (v.second.is_string()) {
  results[v.first] = v.second.as_string();
      }
      else {
  results[v.first] = v.second.serialize();
      }
    }
  }
  return results;
}

/*
  Top-level routine for processing all HTTP POST requests.
 */
void handle_post(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** POST " << path << endl;
  auto paths = uri::split_path(path);
  
  unordered_map<string,string> json_body {get_json_body(message)};
  
  if (paths[0] == push_status) {
  	if (paths.size() < 4) {
  	  message.reply(status_codes::BadRequest);
  	  return;
  	}
  	
  	string friendslist {json_body["Friends"]};
    friends_list_t friendslist_vec = parse_friends_list(friendslist);
  	
  	for (int i = 0; i < friendslist_vec.size(); i++) {
      cout << "Updating " + friendslist_vec[i].first + "/"
              + friendslist_vec[i].second << endl;

  		pair<status_code,value> get_entity {
        do_request(methods::GET,
                  string(addr)
                  + read_entity_admin + "/"
                  + data_table_name + "/"
                  + friendslist_vec[i].first + "/"
                  + friendslist_vec[i].second)
      };
  		
  		string updatelist = get_json_object_prop(get_entity.second, "Updates");
  		updatelist.append(paths[3]);
  		updatelist.append("\n");

      cout << "New Status: " + updatelist << endl;
  		
  		value val = build_json_value("Updates", updatelist);
  		
  		pair<status_code,value> update_entity {
        do_request(methods::PUT,
                  string(addr)
                  + update_entity_admin + "/"
                  + data_table_name + "/"
                  + friendslist_vec[i].first + "/"
                  + friendslist_vec[i].second,
			            val)
      };
  	}
	
  	message.reply(status_codes::OK); //went through all friends of this user and updated their updatelist
  	return;
  }
  else {
	  message.reply(status_codes::BadRequest);
	  return;
  }
}

void handle_get(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** GET " << path << endl;
  message.reply(status_codes::BadRequest);
  return;
}

void handle_put(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PUT " << path << endl;
  message.reply(status_codes::BadRequest);
  return;
}

void handle_delete(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** DELETE " << path << endl;
  message.reply(status_codes::BadRequest);
  return;
}

/*
  Main authentication server routine

  Install handlers for the HTTP requests and open the listener,
  which processes each request asynchronously.

  Note that, unlike BasicServer, PushServer only
  installs the listeners for POST. Any other HTTP
  method will produce a Method Not Allowed (405)
  response.

  If you want to support other methods, uncomment
  the call below that hooks in a the appropriate 
  listener.
  
  Wait for a carriage return, then shut the server down.
 */
int main (int argc, char const * argv[]) {
  cout << "PushServer: Parsing connection string" << endl;

  cout << "PushServer: Opening listener" << endl;
  http_listener listener {def_url};
  //listener.support(methods::GET, &handle_get);
  listener.support(methods::POST, &handle_post);
  //listener.support(methods::PUT, &handle_put);
  //listener.support(methods::DEL, &handle_delete);
  listener.open().wait(); // Wait for listener to complete starting

  cout << "Enter carriage return to stop PushServer." << endl;
  string line;
  getline(std::cin, line);

  // Shut it down
  listener.close().wait();
  cout << "PushServer closed" << endl;
}
