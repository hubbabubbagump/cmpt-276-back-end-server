/*
  Sample unit tests for BasicServer
 */

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <cpprest/http_client.h>
#include <cpprest/json.h>

#include <pplx/pplxtasks.h>

#include <UnitTest++/UnitTest++.h>

#include "ServerUtils.h"
#include "TableCache.h"
#include "make_unique.h"

#include "azure_keys.h"

using std::cerr;
using std::cout;
using std::endl;
using std::make_pair;
using std::pair;
using std::string;
using std::vector;

using web::http::http_headers;
using web::http::http_request;
using web::http::http_response;
using web::http::method;
using web::http::methods;
using web::http::status_code;
using web::http::status_codes;
using web::http::uri_builder;

using web::http::client::http_client;

using web::json::object;
using web::json::value;

using azure::storage::storage_exception;

const string create_table_op {"CreateTableAdmin"};
const string delete_table_op {"DeleteTableAdmin"};

const string read_entity_admin {"ReadEntityAdmin"};
const string update_entity_admin {"UpdateEntityAdmin"};
const string delete_entity_admin {"DeleteEntityAdmin"};

const string read_entity_auth {"ReadEntityAuth"};
const string update_entity_auth {"UpdateEntityAuth"};

const string get_read_token_op  {"GetReadToken"};
const string get_update_token_op {"GetUpdateToken"};
const string get_update_data_op {"GetUpdateData"};

const string sign_on {"SignOn"};
const string sign_off {"SignOff"};
const string add_friend {"AddFriend"};
const string unfriend {"UnFriend"};
const string update_status {"UpdateStatus"};
const string read_friend_list {"ReadFriendList"};

// The two optional operations from Assignment 1
const string add_property_admin {"AddPropertyAdmin"};
const string update_property_admin {"UpdatePropertyAdmin"};

/*
  Make an HTTP request, returning the status code and any JSON value in the body

  method: member of web::http::methods
  uri_string: uri of the request
  req_body: [optional] a json::value to be passed as the message body

  If the response has a body with Content-Type: application/json,
  the second part of the result is the json::value of the body.
  If the response does not have that Content-Type, the second part
  of the result is simply json::value {}.

  You're welcome to read this code but bear in mind: It's the single
  trickiest part of the sample code. You can just call it without
  attending to its internals, if you prefer.
 */

// Version with explicit third argument
pair<status_code,value> do_request (const method& http_method, const string& uri_string, const value& req_body) {
  http_request request {http_method};
  if (req_body != value {}) {
    http_headers& headers (request.headers());
    headers.add("Content-Type", "application/json");
    request.set_body(req_body);
  }

  status_code code;
  value resp_body;
  http_client client {uri_string};
  client.request (request)
    .then([&code](http_response response)
          {
            code = response.status_code();
            const http_headers& headers {response.headers()};
            auto content_type (headers.find("Content-Type"));
            if (content_type == headers.end() ||
                content_type->second != "application/json")
              return pplx::task<value> ([] { return value::object ();});
            else
              return response.extract_json();
          })
    .then([&resp_body](value v) -> void
          {
            resp_body = v;
            return;
          })
    .wait();
  return make_pair(code, resp_body);
}
/*pair<status_code,value> do_request (const method& http_method, const string& uri_string, const value& req_body) {
  http_request request {http_method};
  if (req_body != value {}) {
    http_headers& headers (request.headers());
    headers.add("Content-Type", "application/json");
    request.set_body(req_body);
  }

  status_code code;
  value resp_body;
  http_client client {uri_string};
  client.request (request)
    .then([&code](http_response response)
          {
            code = response.status_code();
            const http_headers& headers {response.headers()};
            auto content_type (headers.find("Content-Type"));
            if (content_type == headers.end() ||
                content_type->second != "application/json")
              return pplx::task<value> ([] { return value {};});
            else
              return response.extract_json();
          })
    .then([&resp_body](value v) -> void
          {
            resp_body = v;
            return;
          })
    .wait();
  return make_pair(code, resp_body);
}*/

// Version that defaults third argument
pair<status_code,value> do_request (const method& http_method, const string& uri_string) {
  return do_request (http_method, uri_string, value {});
}

/*
  Utility to create a table

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
 */
int create_table (const string& addr, const string& table) {
  pair<status_code,value> result {do_request (methods::POST, addr + create_table_op + "/" + table)};
  return result.first;
}

/*
  Utility to compare two JSON objects

  This is an internal routine---you probably want to call compare_json_values().
 */
bool compare_json_objects (const object& expected_o, const object& actual_o) {
  CHECK_EQUAL (expected_o.size (), actual_o.size());
  if (expected_o.size() != actual_o.size())
    return false;

  bool result {true};
  for (auto& exp_prop : expected_o) {
    object::const_iterator act_prop {actual_o.find (exp_prop.first)};
    CHECK (actual_o.end () != act_prop);
    if (actual_o.end () == act_prop)
      result = false;
    else {
      CHECK_EQUAL (exp_prop.second, act_prop->second);
      if (exp_prop.second != act_prop->second)
        result = false;
    }
  }
  return result;
}

/*
  Utility to compare two JSON objects represented as values

  expected: json::value that was expected---must be an object
  actual: json::value that was actually returned---must be an object
*/
bool compare_json_values (const value& expected, const value& actual) {
  assert (expected.is_object());
  assert (actual.is_object());

  object expected_o {expected.as_object()};
  object actual_o {actual.as_object()};
  return compare_json_objects (expected_o, actual_o);
}

/*
  Utility to compre expected JSON array with actual

  exp: vector of objects, sorted by Partition/Row property 
    The routine will throw if exp is not sorted.
  actual: JSON array value of JSON objects
    The routine will throw if actual is not an array or if
    one or more values is not an object.

  Note the deliberate asymmetry of the how the two arguments are handled:

  exp is set up by the test, so we *require* it to be of the correct
  type (vector<object>) and to be sorted and throw if it is not.

  actual is returned by the database and may not be an array, may not
  be values, and may not be sorted by partition/row, so we have
  to check whether it has those characteristics and convert it 
  to a type comparable to exp.
*/
bool compare_json_arrays(const vector<object>& exp, const value& actual) {
  /*
    Check that expected argument really is sorted and
    that every value has Partion and Row properties.
    This is a precondition of this routine, so we throw
    if it is not met.
  */
  auto comp = [] (const object& a, const object& b) -> bool {
    return a.at("Partition").as_string()  <  b.at("Partition").as_string()
       ||
       (a.at("Partition").as_string() == b.at("Partition").as_string() &&
        a.at("Row").as_string()       <  b.at("Row").as_string()); 
  };
  if ( ! std::is_sorted(exp.begin(),
                         exp.end(),
                         comp))
    throw std::exception();

  // Check that actual is an array
  CHECK(actual.is_array());
  if ( ! actual.is_array())
    return false;
  web::json::array act_arr {actual.as_array()};

  // Check that the two arrays have same size
  CHECK_EQUAL(exp.size(), act_arr.size());
  if (exp.size() != act_arr.size())
    return false;

  // Check that all values in actual are objects
  bool all_objs {std::all_of(act_arr.begin(),
                             act_arr.end(),
                             [] (const value& v) { return v.is_object(); })};
  CHECK(all_objs);
  if ( ! all_objs)
    return false;

  // Convert all values in actual to objects
  vector<object> act_o {};
  auto make_object = [] (const value& v) -> object {
    return v.as_object();
  };
  std::transform (act_arr.begin(), act_arr.end(), std::back_inserter(act_o), make_object);

  /* 
     Ensure that the actual argument is sorted.
     Unlike exp, we cannot assume this argument is sorted,
     so we sort it.
   */
  std::sort(act_o.begin(), act_o.end(), comp);

  // Compare the sorted arrays
  bool eq {std::equal(exp.begin(), exp.end(), act_o.begin(), &compare_json_objects)};
  CHECK (eq);
  return eq;
}

/*
  Utility to create JSON object value from vector of properties
*/
value build_json_object (const vector<pair<string,string>>& properties) {
    value result {value::object ()};
    for (auto& prop : properties) {
      result[prop.first] = value::string(prop.second);
    }
    return result;
}

/*
  Utility to delete a table

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
 */
int delete_table (const string& addr, const string& table) {
  // SIGH--Note that REST SDK uses "methods::DEL", not "methods::DELETE"
  pair<status_code,value> result {
    do_request (methods::DEL,
                addr + delete_table_op + "/" + table)};
  return result.first;
}

/*
  Utility to put an entity with a single property

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity 
  row: Row of the entity
  prop: Name of the property
  pstring: Value of the property, as a string
 */
int put_entity(const string& addr, const string& table, const string& partition, const string& row, const string& prop, const string& pstring) {
  pair<status_code,value> result {
    do_request (methods::PUT,
                addr + update_entity_admin + "/" + table + "/" + partition + "/" + row,
                value::object (vector<pair<string,value>>
                               {make_pair(prop, value::string(pstring))}))};
  return result.first;
}

/*
  Utility to put an entity with multiple properties

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity 
  row: Row of the entity
  props: vector of string/value pairs representing the properties
 */
int put_entity(const string& addr, const string& table, const string& partition, const string& row,
              const vector<pair<string,value>>& props) {
  pair<status_code,value> result {
    do_request (methods::PUT,
               addr + update_entity_admin + "/" + table + "/" + partition + "/" + row,
               value::object (props))};
  return result.first;
}

/*
  Utility to delete an entity

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity 
  row: Row of the entity
 */
int delete_entity (const string& addr, const string& table, const string& partition, const string& row)  {
  // SIGH--Note that REST SDK uses "methods::DEL", not "methods::DELETE"
  pair<status_code,value> result {
    do_request (methods::DEL,
                addr + delete_entity_admin + "/" + table + "/" + partition + "/" + row)};
  return result.first;
}

/*
  Utility to get a token good for updating a specific entry
  from a specific table for one day.
 */
pair<status_code,string> get_update_token(const string& addr,  const string& userid, const string& password) {
  value pwd {build_json_object (vector<pair<string,string>> {make_pair("Password", password)})};
  pair<status_code,value> result {do_request (methods::GET,
                                              addr +
                                              get_update_token_op + "/" +
                                              userid,
                                              pwd
                                              )
  };
  cerr << "token " << result.second << endl;
  if (result.first != status_codes::OK) {
    return make_pair (result.first, "");
  }
  else {
    string token {result.second.as_string()};
    return make_pair (result.first, token);
  }
}

pair<status_code,string> get_read_token(const string& addr,  const string& userid, const string& password) {
  value pwd {build_json_object (vector<pair<string,string>> {make_pair("Password", password)})};
  pair<status_code,value> result {do_request (methods::GET,
                                              addr +
                                              get_read_token_op + "/" +
                                              userid,
                                              pwd
                                              )
  };
  cerr << "token " << result.second << endl;
  if (result.first != status_codes::OK) {
    return make_pair (result.first, "");
  }
  else {
    string token {result.second.as_string()};
    return make_pair (result.first, token);
  }
}

/*
  A sample fixture that ensures TestTable exists, and
  at least has the entity Franklin,Aretha/USA
  with the property "Song": "RESPECT".

  The entity is deleted when the fixture shuts down
  but the table is left. See the comments in the code
  for the reason for this design.
 */
class BasicFixture {
public:
  static constexpr const char* addr {"http://localhost:34568/"};
  static constexpr const char* table {"TestTable"};
  static constexpr const char* partition {"USA"};
  static constexpr const char* row {"Franklin,Aretha"};
  static constexpr const char* property {"Song"};
  static constexpr const char* prop_val {"RESPECT"};

public:
  BasicFixture() {
    int make_result {create_table(addr, table)};
    cerr << "create result " << make_result << endl;
    if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
      throw std::exception();
    }
    int put_result {put_entity (addr, table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    if (put_result != status_codes::OK) {
      throw std::exception();
    }
  }

  ~BasicFixture() {
    int del_ent_result {delete_entity (addr, table, partition, row)};
    if (del_ent_result != status_codes::OK) {
      throw std::exception();
    }

    /*
      In traditional unit testing, we might delete the table after every test.

      However, in cloud NoSQL environments (Azure Tables, Amazon DynamoDB)
      creating and deleting tables are rate-limited operations. So we
      leave the table after each test but delete all its entities.
    */
    cout << "Skipping table delete" << endl;
    /*
      int del_result {delete_table(addr, table)};
      cerr << "delete result " << del_result << endl;
      if (del_result != status_codes::OK) {
        throw std::exception();
      }
    */
  }
};

SUITE(GET) {
  /*
    A test of GET all table entries

    Demonstrates use of new compare_json_arrays() function.
   */
  TEST_FIXTURE(BasicFixture, GetAll) {
    cout << ">> GetAll (assign2) Test" << endl;

    string partition {"CAN"};
    string row {"Katherines,The"};
    string property {"Home"};
    string prop_val {"Vancouver"};
    int put_result {put_entity (BasicFixture::addr, BasicFixture::table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    assert (put_result == status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::GET,
                  string(BasicFixture::addr)
                  + read_entity_admin + "/"
                  + string(BasicFixture::table))
    };

    CHECK_EQUAL(status_codes::OK, result.first);

    value obj1 {
      value::object(vector<pair<string,value>> {
          make_pair(string("Partition"), value::string(partition)),
          make_pair(string("Row"), value::string(row)),
          make_pair(property, value::string(prop_val))
      })
    };

    value obj2 {
      value::object(vector<pair<string,value>> {
          make_pair(string("Partition"), value::string(BasicFixture::partition)),
          make_pair(string("Row"), value::string(BasicFixture::row)),
          make_pair(string(BasicFixture::property), value::string(BasicFixture::prop_val))
      })
    };

    vector<object> exp {
      obj1.as_object(),
      obj2.as_object()
    };

    compare_json_arrays(exp, result.second);
    CHECK_EQUAL(status_codes::OK, delete_entity (BasicFixture::addr, BasicFixture::table, partition, row));
  }

   /*
      A test of GET of a single entity
   */
   TEST_FIXTURE(BasicFixture, GetSingle) {
      cout << ">> GetSingle test" << endl;
      pair<status_code,value> result {
         do_request (methods::GET,
        string(BasicFixture::addr)
        + read_entity_admin + "/"
        + BasicFixture::table + "/"
        + BasicFixture::partition + "/"
        + BasicFixture::row)
      };

   //Formatting changed to fit
   CHECK_EQUAL(string("{\"")
 /*     + "Partition" + "\":\""
      + BasicFixture::partition + "\",\""
      + "Row" + "\":\""
      + BasicFixture::row + "\",\""    */
      + BasicFixture::property + "\":\"" 
      + BasicFixture::prop_val + "\"" 
      + "}",  
      result.second.serialize());
   CHECK_EQUAL(status_codes::OK, result.first);
  }

   /*
      A test of GET of an entity with table name, partition name, with row name as '*'
   */
   TEST_FIXTURE(BasicFixture, GetPartition) {
      cout << ">> GetPartition test" << endl;

      string partition {"USA"};
      string row {"John,Doe"};
      string property {"Song"};
      string prop_val {"DISRESPECT"};
      int put_result {put_entity (BasicFixture::addr, BasicFixture::table, partition, row, property, prop_val)};
      cerr << "put result " << put_result << endl;
      assert (put_result == status_codes::OK);

      string partition2 {"CAN"};
      string row2 {"Katherines,The"};
      string property2 {"Home"};
      string prop_val2 {"Vancouver"};
      int put_result2 {put_entity (BasicFixture::addr, BasicFixture::table, partition2, row2, property2, prop_val2)};
      cerr << "put result " << put_result2 << endl;
      assert (put_result2 == status_codes::OK);

      pair<status_code, value> result {
         do_request (methods::GET, 
            string(BasicFixture::addr)
            + read_entity_admin + "/"
            + BasicFixture::table + "/"
            + BasicFixture::partition + "/"
            + "*")
      };
   
      
      CHECK_EQUAL(string("[{\"")
         + "Partition" + "\":\""
         + BasicFixture::partition + "\",\""
         + "Row" + "\":\""
         + BasicFixture::row + "\",\""
         + BasicFixture::property + "\":\"" 
         + BasicFixture::prop_val + "\"" 
         + "},{\""
         + "Partition" + "\":\""
         + BasicFixture::partition + "\",\""
         + "Row" + "\":\""
         + row + "\",\""
         + property + "\":\"" 
         + prop_val + "\""
         + "}]",
         result.second.serialize());
      

      //CHECK_EQUAL(2, result.second.as_array().size());
      CHECK_EQUAL(status_codes::OK, result.first);
      CHECK_EQUAL(status_codes::OK, delete_entity (BasicFixture::addr, BasicFixture::table, partition, row));
      CHECK_EQUAL(status_codes::OK, delete_entity (BasicFixture::addr, BasicFixture::table, partition2, row2));

   }

   TEST_FIXTURE(BasicFixture, EdgeCases) {
      cout << ">> EdgeCases test" << endl;

      string partition {"CAN"};
      string row {"Franklin,Aretha"};
      string property {"Song"};
      string prop_val {"DISRESPECT"};
      int put_result {put_entity (BasicFixture::addr, BasicFixture::table, partition, row, property, prop_val)};
      cerr << "put result " << put_result << endl;
      assert (put_result == status_codes::OK);

      //Partition does not exist
      cout << "Edge Partition 1" << endl;
      pair<status_code, value> result {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin + "/"
            + BasicFixture::table + "/"
            + "NotA,Partition" + "/"
            + "*")
      };
      CHECK_EQUAL(status_codes::BadRequest, result.first);

      //Row does not exist/is not '*'
      cout << "Edge Partition 2" << endl;
      pair<status_code, value> result2 {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin + "/"
            + BasicFixture::table + "/"
            + BasicFixture::partition + "/"
            + "NotA,Row")
      };
      CHECK_EQUAL(status_codes::NotFound, result2.first);

      //Table does not exist
      cout << "Edge Partition 3" << endl;
      pair<status_code, value> result3 {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin + "/"
            + "NotATable" + "/"
            + BasicFixture::partition + "/"
            + BasicFixture::row)
      };
      CHECK_EQUAL(status_codes::NotFound, result3.first);

      //No paths (Missing table, partition, row)
      cout << "Edge Partition 4" << endl;
      pair<status_code, value> result4 {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin)
      };
      CHECK_EQUAL(status_codes::BadRequest, result4.first);

      //Missing Partition
      cout << "Edge Partition 5" << endl;
      pair<status_code, value> result5 {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin + "/"
            + BasicFixture::table + "/"
            + "/" + BasicFixture::row)
      };
      CHECK_EQUAL(status_codes::BadRequest, result5.first);

      //Missing Row
      cout << "Edge Partition 6" << endl;
      pair<status_code, value> result6 {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin + "/"
            + BasicFixture::table + "/"
            + BasicFixture::partition)
      };
      CHECK_EQUAL(status_codes::BadRequest, result6.first);

      //Missing Row and Partition, with wrong Table name
      cout << "Edge Partition 7" << endl;
      pair<status_code, value> result7 {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin + "/"
            + "NotATable")
      };
      CHECK_EQUAL(status_codes::NotFound, result7.first);

      //Wrong table, partition, and row
      cout << "Edge Partition 8" << endl;
      pair<status_code, value> result8 {
        do_request (methods::GET,
          string(BasicFixture::addr)
          + read_entity_admin + "/"
          + "NotATable" + "/"
          + "NotA,Partition" + "/"
          + "NotA,Row")
      };
      CHECK_EQUAL(status_codes::NotFound, result8.first);

      CHECK_EQUAL(status_codes::OK, delete_entity (BasicFixture::addr, BasicFixture::table, partition, row));
   }

    /*
      A test of Get JSON properties
   */
   TEST_FIXTURE(BasicFixture, GetJSON) {
      cout << ">> GetJSON test" << endl;

      string partition {"CAN"};
      string row {"Franklin,Aretha"};
      string property {"Song"};
      string prop_val {"DISRESPECT"};
      int put_result {put_entity (BasicFixture::addr, BasicFixture::table, partition, row, property, prop_val)};
      cerr << "put result " << put_result << endl;
      assert (put_result == status_codes::OK);

      string partition2 {"CAN"};
      string row2 {"Katherines,The"};
      string property2 {"Home"};
      string prop_val2 {"Vancouver"};
      int put_result2 {put_entity (BasicFixture::addr, BasicFixture::table, partition2, row2, property2, prop_val2)};
      cerr << "put result " << put_result2 << endl;
      assert (put_result2 == status_codes::OK);


      
      pair<status_code, value> result {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin + "/"
            + BasicFixture::table,
            value::object (vector<pair<string,value>>
                {make_pair("Song", value::string("Respect"))}))
      };  
      CHECK_EQUAL(2, result.second.as_array().size());
      CHECK_EQUAL(string("[{\"")
         + "Partition" + "\":\""
         + partition + "\",\""
         + "Row" + "\":\""
         + row + "\",\""
         + property + "\":\"" 
         + prop_val + "\"" 
         + "},{\""
         + "Partition" + "\":\""
         + BasicFixture::partition + "\",\""
         + "Row" + "\":\""
         + BasicFixture::row + "\",\""
         + BasicFixture::property + "\":\"" 
         + BasicFixture::prop_val + "\""
         + "}]",
         result.second.serialize());

      //Property not found
      cout << "Edge JSON 1" << endl;
      pair<status_code, value> result2 {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin + "/"
            + BasicFixture::table,
            value::object (vector<pair<string,value>>
                {make_pair("NotASong", value::string("string"))}))
      };     
      CHECK_EQUAL(status_codes::BadRequest, result2.first);

      //No Table value
      cout << "Edge JSON 2" << endl;
      pair<status_code, value> result3 {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin,
            value::object (vector<pair<string,value>>
                {make_pair("NotASong", value::string("string"))}))
      };
      CHECK_EQUAL(status_codes::BadRequest, result3.first);

      //Table not found
      cout << "Edge JSON 3" << endl;
      pair<status_code, value> result4 {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin + "/"
            + "NotA,Table",
            value::object (vector<pair<string,value>>
                {make_pair("Home", value::string("string"))}))
      };
      CHECK_EQUAL(status_codes::NotFound, result4.first);

      //Random prop_val and different property (Katherine's)
      cout << "Edge JSON 4" << endl;
      pair<status_code, value> result5 {
         do_request (methods::GET,
            string(BasicFixture::addr)
            + read_entity_admin + "/"
            + BasicFixture::table,
            value::object (vector<pair<string,value>>
                {make_pair("Home", value::string("KAHD872f273f72kauhfsefKAHDA&Y*Y@#*uygQETR"))}))
      };
      CHECK_EQUAL(status_codes::OK, result5.first);
      CHECK_EQUAL(1, result5.second.as_array().size());

      CHECK_EQUAL(status_codes::OK, delete_entity (BasicFixture::addr, BasicFixture::table, partition, row));
      CHECK_EQUAL(status_codes::OK, delete_entity (BasicFixture::addr, BasicFixture::table, partition2, row2));

   }

   /*
      A test of GET all table entries
   */
   TEST_FIXTURE(BasicFixture, GetAllAssign1) {
      cout << ">> GetAll (assign1) test" << endl;

      string partition {"Katherines,The"};
      string row {"Katherines,The"};
      string property {"Home"};
      string prop_val {"Vancouver"};
      int put_result {put_entity (BasicFixture::addr, BasicFixture::table, partition, row, property, prop_val)};
      cerr << "put result " << put_result << endl;
      assert (put_result == status_codes::OK);

      pair<status_code,value> result {
         do_request (methods::GET,
       string(BasicFixture::addr)
       + read_entity_admin + "/"
       + string(BasicFixture::table))
      };
      
      CHECK(result.second.is_array());
      //cout << result.second << endl;
      //cout << result.second.serialize() << endl;
      CHECK_EQUAL(2, result.second.as_array().size());
      /*
         Checking the body is not well-supported by UnitTest++, as we have to test
         independent of the order of returned values.
      */
      //CHECK_EQUAL(body.serialize(), string("{\"")+string(BasicFixture::property)+ "\":\""+string(BasicFixture::prop_val)+"\"}");
      CHECK_EQUAL(status_codes::OK, result.first);
      CHECK_EQUAL(status_codes::OK, delete_entity (BasicFixture::addr, BasicFixture::table, partition, row));
   }
}

class AuthFixture {
public:
  static constexpr const char* addr {"http://localhost:34568/"};
  static constexpr const char* auth_addr {"http://localhost:34570/"};
  static constexpr const char* userid {"user"};
  static constexpr const char* user_pwd {"user"};
  static constexpr const char* auth_table {"AuthTable"};
  static constexpr const char* auth_table_partition {"Userid"};
  static constexpr const char* auth_pwd_prop {"Password"};
  static constexpr const char* table {"DataTable"};
  static constexpr const char* partition {"USA"};
  static constexpr const char* row {"Franklin,Aretha"};
  static constexpr const char* property {"Song"};
  static constexpr const char* prop_val {"RESPECT"};

public:
  AuthFixture() {
    int make_result {create_table(addr, table)};
    cerr << "create result " << make_result << endl;
    if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
      throw std::exception();
    }

    //Change properties to Friends/Status/Updates in new Fixture
    int put_result {put_entity (addr, table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    if (put_result != status_codes::OK) {
      throw std::exception();
    }

    vector<pair<string, value>> v {
      make_pair("Password", value::string(user_pwd)),
      make_pair("DataPartition", value::string(partition)),
      make_pair("DataRow", value::string(row))
    };

    // Ensure userid and password in system
    int user_result {put_entity (addr, auth_table, auth_table_partition, userid, v)};
    cerr << "user auth table insertion result " << user_result << endl;
    if (user_result != status_codes::OK)
      throw std::exception();
  }

  ~AuthFixture() {
    int del_ent_result {delete_entity (addr, table, partition, row)};
    if (del_ent_result != status_codes::OK) {
      throw std::exception();
    }
  }
};

SUITE(UPDATE_AUTH) {
  TEST_FIXTURE(AuthFixture,  PutAuth) {
    cout << ">> PutAuth Test" << endl;

    pair<string,string> added_prop {make_pair(string("born"),string("1942"))};

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)
    };
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL ( status_codes::OK, token_res.first);
    
    //read_entity_auth is the token for reading
    pair<status_code,value> result {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )
    };
    CHECK_EQUAL(status_codes::OK, result.first);
    
    pair<status_code,value> ret_res {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_admin + "/"
                  + AuthFixture::table + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)
    };
    CHECK_EQUAL (status_codes::OK, ret_res.first);


    value expect1 {
      build_json_object (
                          vector<pair<string,string>> {
                            added_prop,
                            make_pair(string(AuthFixture::property), 
                                      string(AuthFixture::prop_val))
                          }
      )
    };

    //cout << ret_res.second.serialize() << endl;
    //cout << expect1 << endl;
                             
    compare_json_values (expect1, ret_res.second);

    //Less than four parameters
    cout << "Edge PUT_AUTH 1" << endl;
    pair<status_code,value> result2 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second,
                  value::object (vector<pair<string,value>> 
                    {make_pair(added_prop.first, 
                    value::string(added_prop.second))})
                  )
    };
    CHECK_EQUAL(status_codes::BadRequest, result2.first);

    //Token for reading
    cout << "Edge PUT_AUTH 2" << endl;
    try {
      pair<status_code,value> result3 {
        do_request (methods::PUT,
                    string(AuthFixture::addr)
                    + read_entity_auth + "/"
                    + AuthFixture::table + "/"
                    + token_res.second + "/"
                    + AuthFixture::partition + "/"
                    + AuthFixture::row,
                      value::object (vector<pair<string,value>>
                        {make_pair(added_prop.first,
                        value::string(added_prop.second))})
                    )
      };
      //cout << "No exception thrown: Exception expected" << endl;
      CHECK_EQUAL(status_codes::Forbidden, result3.first);
    }
    catch (const storage_exception& e) {
      cout << "Azure Table Storage error: " << e.what() << endl;
      cout << e.result().extended_error().message() << endl;
      if (e.result().http_status_code() == status_codes::Forbidden) 
        cout << "Exception: " << status_codes::Forbidden << endl;
      else
        cout << "Exception: " << status_codes::InternalError << endl;
    }

    //Token does not authorize access
    cout << "Edge PUT_AUTH 3" << endl;
    pair<status_code,value> result4 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_admin + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )
    };
    CHECK_EQUAL(status_codes::NotFound, result4.first);

    //Table was not found
    cout << "Edge PUT_AUTH 4" << endl;
    pair<status_code,value> result5 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + "NotATable" + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )
    };
    CHECK_EQUAL(status_codes::NotFound, result5.first);

    //No entity with partition and row name
    cout << "Edge PUT_AUTH 5" << endl;
    pair<status_code,value> result6 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + "Bob" + "/"
                  + "Jenkins",
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )
    };
    CHECK_EQUAL(status_codes::Forbidden, result6.first);


  }
}

SUITE(GET_AUTH) {
  TEST_FIXTURE(AuthFixture, GetAuth) {
    cout << ">> GetAuth Test" << endl;

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_read_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)
    };

    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL ( status_codes::OK, token_res.first);
      
    pair<status_code,value> result {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row
      )
    };
    //cout << result.second.serialize() << endl;

    vector<pair<string, string>> expect {
      make_pair("SONG", "RESPECT")
    };
    value expect1 {
      build_json_object (expect)
    };
              
    CHECK_EQUAL (string("{\"")
      + AuthFixture::property + "\":\""
      + AuthFixture::prop_val + "\"}",
      result.second.serialize());

    //Less than 4 parameters
    cout << "Edge GET_AUTH 1" << endl;
    pair<status_code, value> result2 {
      do_request (methods::GET,
                    string(AuthFixture::addr)
                    + read_entity_auth + "/"
                    + AuthFixture::table + "/"
                    + token_res.second + "/"
                    + AuthFixture::partition
        )
    };
    CHECK_EQUAL(status_codes::BadRequest, result2.first);


    //Table does not exist
    cout << "Edge GET_AUTH 2" << endl;
    pair<status_code, value> result3 {
      do_request (methods::GET,
                    string(AuthFixture::addr)
                    + read_entity_auth + "/"
                    + "NotATable" + "/"
                    + token_res.second + "/"
                    + AuthFixture::partition + "/"
                    + AuthFixture::row
        )
    };
    CHECK_EQUAL(status_codes::NotFound, result3.first);

    //partition and row does not retrieve anything
    cout << "Edge GET_AUTH 3" << endl;
    pair<status_code, value> result4 {
      do_request (methods::GET,
                    string(AuthFixture::addr)
                    + read_entity_auth + "/"
                    + AuthFixture::table + "/"
                    + token_res.second + "/"
                    + "NotA,Partition" + "/"
                    + "NotARow"
        )
    };
    CHECK_EQUAL(status_codes::NotFound, result4.first);

    //token does not authorize access
    cout << "Edge GET_AUTH 4" << endl;
    pair<status_code, value> result5 {
      do_request (methods::GET,
                    string(AuthFixture::addr)
                    + read_entity_auth + "/"
                    + AuthFixture::table + "/"
                    + "UnauthorizedToken" + "/"
                    + AuthFixture::partition + "/"
                    + AuthFixture::row
        )
    };
    CHECK_EQUAL(status_codes::NotFound, result5.first);

    //using admin instead of auth
    cout << "Edge GET_AUTH 5" << endl;
    pair<status_code, value> result6 {
      do_request (methods::GET,
                    string(AuthFixture::addr)
                    + read_entity_admin + "/"
                    + AuthFixture::table + "/"
                    + token_res.second + "/"
                    + AuthFixture::partition + "/"
                    + AuthFixture::row
        )
    };
    CHECK_EQUAL(status_codes::BadRequest, result6.first);

  }

}

SUITE(AUTH) {
  TEST_FIXTURE(AuthFixture, Auth) {
    //Function for read is the same as update (just copied
    //and pasted), so no need to  run more tests for read.
    //Only difference is that it pipes a read op instead of
    //an update op.

    cout << ">> Auth Test" << endl;

     //Invalid userID
    cout << "Test AUTH 1" << endl;
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       "NotAUserID",
                       AuthFixture::user_pwd)
    };
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL ( status_codes::NotFound, token_res.first);
  
    //Invalid password
    cout << "Test AUTH 2" << endl;
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res2 {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       "NotAPassword")
    };
    cout << "Token response " << token_res2.first << endl;
    CHECK_EQUAL ( status_codes::NotFound, token_res2.first);
    
    //Invalid credentials
    cout << "Test AUTH 3" << endl;
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res3 {
      get_update_token(AuthFixture::auth_addr,
                       "NotAUserID",
                       "NotAPassword")
    };
    cout << "Token response " << token_res3.first << endl;
    CHECK_EQUAL ( status_codes::NotFound, token_res3.first);

    //Wrong address
    cout << "Test AUTH 4" << endl;
    try {
      cout << "Requesting token" << endl;
      pair<status_code,string> token_res4 {
        get_update_token(AuthFixture::addr,
                         AuthFixture::userid,
                         AuthFixture::user_pwd)
      };
      cout << "Token response " << token_res4.first << endl;
      CHECK_EQUAL(status_codes::NotFound, token_res4.first);
    }
    catch(const storage_exception& e) {
      cout << "Exception occured" << endl;
    }
  }
}

class UserFixture {
public:
  static constexpr const char* addr {"http://localhost:34568/"};
  static constexpr const char* auth_addr {"http://localhost:34570/"};
  static constexpr const char* user_addr {"http://localhost:34572/"};
  static constexpr const char* push_addr {"http://localhost:34574/"};
  static constexpr const char* userid {"Gary"};
  static constexpr const char* user_pwd {"Stu"};
  static constexpr const char* auth_table {"AuthTable"};
  static constexpr const char* auth_table_partition {"Userid"};
  static constexpr const char* auth_pwd_prop {"Password"};
  static constexpr const char* table {"DataTable"};
  static constexpr const char* partition {"CAN"};
  static constexpr const char* row {"Stu,Gary"};

  static constexpr const char* friends {"Friends"};
  static constexpr const char* friends_val {"USA;Shinoda,Mike"};
  static constexpr const char* status {"Status"};
  static constexpr const char* status_val {"I%20Suck"};
  static constexpr const char* updates {"Updates"};
  static constexpr const char* updates_val {"Status Updates\n"};

public:
  UserFixture() {
    int make_result {create_table(addr, table)};
    cerr << "create result " << make_result << endl;
    if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
      throw std::exception();
    }

    vector<pair<string, value>> v1 {
      make_pair("Friends", value::string(friends_val)),
      make_pair("Status", value::string(status_val)),
      make_pair("Updates", value::string(updates_val))
    };

    //Change properties to Friends/Status/Updates in new Fixture
    int put_result {put_entity (addr, table, partition, row, v1)};
    cerr << "put result " << put_result << endl;
    if (put_result != status_codes::OK) {
      throw std::exception();
    }

    vector<pair<string, value>> v2 {
      make_pair("Password", value::string(user_pwd)),
      make_pair("DataPartition", value::string(partition)),
      make_pair("DataRow", value::string(row))
    };

    // Ensure userid and password in system
    int user_result {put_entity (addr,
                                 auth_table,
                                 auth_table_partition,
                                 userid,
                                 v2)};
    cerr << "user auth table insertion result " << user_result << endl;
    if (user_result != status_codes::OK)
      throw std::exception();
  }

  ~UserFixture() {
    int del_ent_result {delete_entity (addr, table, partition, row)};
    if (del_ent_result != status_codes::OK) {
      throw std::exception();
    }
  }
};

SUITE(USER) {
  TEST_FIXTURE(UserFixture, SignOn) {
    cout << ">> SignOn Test" << endl;

    pair<status_code, value> result {
      do_request (methods::POST,
                  string(UserFixture::user_addr)
                  + sign_on + "/"
                  + userid,
                  value::object (vector<pair<string,value>>
                                   {make_pair("Password",
                                              value::string(user_pwd))}))
    };                                                     //Stu

    //wrong password
    cout << "Edge SignOn 1" << endl;
    pair<status_code, value> result2 {
      do_request (methods::POST,
                  string(UserFixture::user_addr)
                  + sign_on + "/"
                  + userid,
                  value::object (vector<pair<string,value>>
                                   {make_pair("Password",
                                              value::string("WrongPassword"))}))
    };
    CHECK_EQUAL(status_codes::NotFound, result2.first);

    //Wrong property
    cout << "Edge SignOn 2" << endl;
    pair<status_code, value> result3 {
      do_request (methods::POST,
                  string(UserFixture::user_addr)
                  + sign_on + "/"
                  + userid,
                  value::object (vector<pair<string,value>>
                                   {make_pair("WrongProperty",
                                              value::string(user_pwd))}))
    };
    CHECK_EQUAL(status_codes::NotFound, result3.first);

    //Wrong operation
    //Also tests for SignOff
    cout << "Edge SignOn 3" << endl;
    pair<status_code, value> result4 {
      do_request (methods::POST,
                  string(UserFixture::user_addr)
                  + "sign_up" + "/"
                  + userid,
                  value::object (vector<pair<string,value>>
                                   {make_pair("Password",
                                              value::string(user_pwd))}))
    };
    CHECK_EQUAL(status_codes::BadRequest, result4.first);

    //user does not exist
    cout << "Edge SignOn 4" << endl;
    pair<status_code, value> result5 {
      do_request (methods::POST,
                  string(UserFixture::user_addr)
                  + sign_on + "/"
                  + "WrongID",
                  value::object (vector<pair<string,value>>
                                   {make_pair("Password",
                                              value::string(user_pwd))}))
    };
    CHECK_EQUAL(status_codes::NotFound, result5.first);

    //already signed in, sign in again with correct login
    cout << "Edge SignOn 5" << endl;
    pair<status_code, value> result6 {
      do_request (methods::POST,
                  string(UserFixture::user_addr)
                  + sign_on + "/"
                  + userid,
                  value::object (vector<pair<string,value>>
                                   {make_pair("Password",
                                              value::string(user_pwd))}))
    };
    CHECK_EQUAL(status_codes::OK, result6.first);
  
    //already signed in, sign in again with wrong password
    cout << "Edge SignOn 6" << endl;
    pair<status_code, value> result7 {
      do_request (methods::POST,
                  string(UserFixture::user_addr)
                  + sign_on + "/"
                  + userid,
                  value::object (vector<pair<string,value>>
                                   {make_pair("Password",
                                              value::string("WrongPassword"))}))
    };
    CHECK_EQUAL(status_codes::NotFound, result7.first);
  }

  TEST_FIXTURE(UserFixture, GetUser) {
    cout << ">> GetUser Test" << endl;

    pair<status_code, value> result {
      do_request (methods::GET,
                  string(UserFixture::user_addr)
                  + read_friend_list + "/"
                  + userid) //Gary
    };
    CHECK_EQUAL(result.first, status_codes::OK);
    CHECK_EQUAL(string("{\"") 
                + friends + "\":\""
                + friends_val + "\"}", 
                result.second.serialize()
    );

    //Wrong operation
    cout << "Edge GetUser 1" << endl;
    pair<status_code, value> result2 {
      do_request (methods::GET,
                  string(UserFixture::user_addr)
                  + "NotAReadOp" + "/"
                  + userid) 
    };
    CHECK_EQUAL(result2.first, status_codes::BadRequest);

    //not logged in
    cout << "Edge GetUser 2" << endl;
    pair<status_code, value> result3 {
      do_request (methods::GET,
                  string(UserFixture::user_addr)
                  + read_friend_list + "/"
                  + "AADAWD") 
    };
    CHECK_EQUAL(result3.first, status_codes::Forbidden);

    //not enough params
    cout << "Edge GetUser 3" << endl;
    pair<status_code, value> param {
      do_request(methods::GET,
                string(UserFixture::user_addr)
                + read_friend_list)
    };
    CHECK_EQUAL(status_codes::BadRequest, param.first);
  }

  TEST_FIXTURE(UserFixture, AddUnFriend) {
    cout << ">> AddFriend Test" << endl;
    
    //cout << "Adding Bob Ross to Tables" << endl;
    string part_country {"AUS"};
    string row_name {"Ross,Bob"};
    string pass = "Ross";
    string uid = "Bob";

    //Add Bob Ross to Franklin's friends list
    pair<status_code, value> result {
      do_request(methods::PUT,
        string(UserFixture::user_addr)
        + add_friend + "/"
        + userid + "/"
        + part_country + "/"
        + row_name)
    };
    CHECK_EQUAL(status_codes::OK, result.first);

    pair<status_code, value> get_friends {
      do_request (methods::GET,
                  string(UserFixture::user_addr)
                  + read_friend_list + "/"
                  + userid) //Gary
    };
    CHECK_EQUAL(status_codes::OK, get_friends.first);
    CHECK_EQUAL(string("{\"") + friends + "\":\""
                + friends_val + "|"
                + part_country + ";"
                + row_name + "\"}",
                get_friends.second.serialize());

    //user is not logged in
    cout << "Edge AddFriend 1" << endl;
    pair<status_code, value> result2 {
      do_request(methods::PUT,
        string(UserFixture::user_addr)
        + add_friend + "/"
        + "NotLoggedIn" + "/"
        + part_country + "/"
        + row_name)
    };
    CHECK_EQUAL(status_codes::Forbidden, result2.first);

    //not enough params
    cout << "Edge AddFriend 2" << endl;
    pair<status_code,value> param1 {
      do_request(methods::PUT,
        string(UserFixture::user_addr)
        + add_friend)
    };
    CHECK_EQUAL(status_codes::BadRequest, param1.first);

    //Adding friend already on friendslist
    cout << "Edge AddFriend 3" << endl;
    pair<status_code, value> result3 {
      do_request(methods::PUT,
        string(UserFixture::user_addr)
        + add_friend + "/"
        + userid + "/"
        + part_country + "/"
        + row_name)
    };
    CHECK_EQUAL(status_codes::OK, result3.first);

    pair<status_code, value> get_friends2 {
      do_request (methods::GET,
                  string(UserFixture::user_addr)
                  + read_friend_list + "/"
                  + userid) //Gary
    };
    CHECK_EQUAL(status_codes::OK, get_friends2.first);
    CHECK_EQUAL(string("{\"") + friends + "\":\""
                + friends_val + "|"
                + part_country + ";"
                + row_name + "\"}",
                get_friends2.second.serialize());

    /////////////////////////////////////////////////
	  cout << ">> UnFriend Test" << endl;
	
	  //Remove Bob Ross from Franklin's friends list
      pair<status_code, value> result1 {
        do_request(methods::PUT,
          string(UserFixture::user_addr)
          + unfriend + "/"
          + userid + "/"
          + part_country + "/"
          + row_name)
    };
    CHECK_EQUAL(status_codes::OK, result1.first);
	
	  pair<status_code, value> get_friends1 {
      do_request (methods::GET,
                  string(UserFixture::user_addr)
                  + read_friend_list + "/"
                  + userid) //Gary
    };
    CHECK_EQUAL(status_codes::OK, get_friends1.first);
    CHECK_EQUAL(string("{\"") + friends + "\":\""
                + friends_val + "\"}",
                get_friends1.second.serialize());

    //user is not logged in
    cout << "Edge UnFriend1" << endl;
    pair<status_code, value> result1_2 {
      do_request(methods::PUT,
        string(UserFixture::user_addr)
        + unfriend + "/"
        + "NotLoggedIn" + "/"
        + part_country + "/"
        + row_name)
    };
	  CHECK_EQUAL(status_codes::Forbidden, result1_2.first);
	
	  string part_country1 {"CN"};
    string row_name1 {"Nimoy,Leonard"};
    string pass1 = "Nimoy";
    string uid1 = "Leonard";

	  //unfriending someone not on their friends list
	  cout << "Edge UnFriend2" << endl;
	  pair<status_code, value> result1_3 {
		  do_request(methods::PUT,
		            string(UserFixture::user_addr)
		            + unfriend + "/"
		            + userid + "/"
                + part_country1 + "/"
                + row_name1)
	  };
	  CHECK_EQUAL(status_codes::OK, result1_3.first);

    //not enough params
    cout << "Edge UnFriend 2" << endl;
    pair<status_code,value> param2 {
      do_request(methods::PUT,
        string(UserFixture::user_addr)
        + unfriend)
    };
    CHECK_EQUAL(status_codes::BadRequest, param2.first);	
  }
  
  TEST_FIXTURE(UserFixture, StatusUpdate) {
	  cout << ">> UpdateStatus Test" << endl;
	  
    //Add Bob to DataTable
	  string new_status {"NewStatus"};
    string part_country {"AUS"};
    string row_name {"Ross,Bob"};
    string pass = "Ross";
    string uid = "Bob";

    string friend_val {"USA;Shinoda,Mike"};
    string stat_val {"You%20Suck"};
    string update_val {"CurrentlyNothing\n"};


    vector<pair<string, value>> v1 {
      make_pair("Friends", value::string(friend_val)),
      make_pair("Status", value::string(stat_val)),
      make_pair("Updates", value::string(update_val))
    };

    int put_result {put_entity (UserFixture::addr, 
                                UserFixture::table, 
                                part_country, 
                                row_name, 
                                v1)
    };
    cerr << "put result " << put_result << endl;
    assert (put_result == status_codes::OK); 

    //Add bob to franklin's FL
    pair<status_code, value> add_bob {
      do_request(methods::PUT,
        string(UserFixture::user_addr)
        + add_friend + "/"
        + userid + "/"
        + part_country + "/"
        + row_name)
    };
    CHECK_EQUAL(status_codes::OK, add_bob.first);
	  
    //Update Franklin's status
	  pair<status_code, value> result {
      do_request(methods::PUT,
        string(UserFixture::user_addr)
        + update_status + "/"
        + userid + "/"
        + new_status)
    };
    if (result.first == status_codes::OK){
      cout << "Status update successful" << endl;
    }
    else if (result.first == status_codes::ServiceUnavailable) {
      cout << "PushServer is down" << endl;
    }
    else {
      cout << "Status Update unsuccessful: " << result.first << endl;
    }
  
    //Get Bob
    pair<status_code, value> get_entities {
      do_request(methods::GET,
        string(UserFixture::addr)
        + read_entity_admin + "/"
        + table + "/"
        + part_country + "/"
        + row_name)
    };

    CHECK_EQUAL(string("{\"")
                + friends + "\":\""
                + friends_val + "\",\""
                + status + "\":\""
                + stat_val + "\",\""
                + updates + "\":\""
                + "CurrentlyNothing" + "\\n" + new_status + "\\n" + "\"}",
                get_entities.second.serialize());

    //malformed request for pushserver
    cout << "Edge UpdateStatus 1" << endl;
    pair<status_code, value> mal_req {
      do_request(methods::PUT,
                string(UserFixture::user_addr)
                + "DoSomething" + "/"
                + userid + "/"
                + new_status)
    };
    CHECK_EQUAL(status_codes::BadRequest, mal_req.first);

    //invalid userid
    cout << "Edge UpdateStatus 2" << endl;
    pair<status_code, value> bad_id {
      do_request(methods::PUT,
                string(UserFixture::user_addr)
                + update_status + "/"
                + "HUEHUEHUE" + "/"
                + new_status)
    };
    CHECK_EQUAL(status_codes::Forbidden, bad_id.first);

    //not enough parameters
    cout << "Edge UpdateStatus 3" << endl;
    pair<status_code, value> param {
      do_request(methods::PUT,
                string(UserFixture::user_addr)
                + update_status + "/"
                + userid)
    };
    CHECK_EQUAL(status_codes::BadRequest, param.first);

    CHECK_EQUAL(status_codes::OK, delete_entity(UserFixture::addr, UserFixture::table, "USA", "Shinoda,Mike"));
    CHECK_EQUAL(status_codes::OK, delete_entity (UserFixture::addr, 
                                                UserFixture::table, 
                                                part_country, 
                                                row_name));
  }

  TEST_FIXTURE(UserFixture, SignOff) {
    cout << ">> SignOff Test" << endl;

    pair<status_code, value> result {
      do_request (methods::POST,
                  string(UserFixture::user_addr)
                  + sign_off + "/"
                  + userid)
    };

    CHECK_EQUAL(status_codes::OK, result.first);

    //userid not logged in
    cout << "Edge SignOff 1" << endl;
    pair<status_code, value> result1 {
      do_request (methods::POST,
                  string(UserFixture::user_addr)
                  + sign_off + "/"
                  + "Bleh")
    };
    CHECK_EQUAL(status_codes::NotFound, result1.first);

    //signout with valid id but no active session
    cout << "Edge SignOff 2" << endl;
    pair<status_code, value> result2 {
      do_request (methods::POST,
                  string(UserFixture::user_addr)
                  + sign_off + "/"
                  + "userid")
    };
    CHECK_EQUAL(status_codes::NotFound, result2.first);

    //not enough parameters
    cout << "Edge SignOff 3" << endl;
    pair<status_code, value> result3 {
      do_request (methods::POST,
                  string(UserFixture::user_addr)
                  + sign_off)
    };
    CHECK_EQUAL(status_codes::NotFound, result3.first);
  }
}

//Checks for disallowed methods
SUITE(USER_DIS) {
  TEST_FIXTURE(UserFixture, rand) {
    pair<status_code, value> result {
      do_request (methods::DEL,
                  string(UserFixture::user_addr))
    };
    CHECK_EQUAL(status_codes::MethodNotAllowed, result.first);
    
    pair<status_code, value> result2 {
      do_request (methods::GET,
                  string(UserFixture::push_addr))
    };
    CHECK_EQUAL(status_codes::MethodNotAllowed, result2.first);
  }
}

   