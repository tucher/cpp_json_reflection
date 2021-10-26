#include "cpp_json_reflection.hpp"
#include <list>
#include "test_utils.hpp"

#include "fstream"
#include <string.h>

using JSONReflection::J;
using std::vector, std::list, std::array, std::string, std::int64_t;
namespace  Twi{


struct UserObject {
    J<int64_t,             "id">        id;
    J<string,              "id_str">    id_str;
    J<string,              "name">    name;
    J<string,              "string_name">    string_name;
    J<string,              "location">    location;
    J<string,              "url">    url;
    J<string,              "description">    description;
    J<bool,                "protected">        protected_;
    J<bool,                "verified">        verified;
    J<int64_t,             "followers_count">        followers_count;
    J<int64_t,             "friends_count">        friends_count;
    J<int64_t,             "listed_count">        listed_count;
    J<int64_t,             "favourites_count">        favourites_count;
    J<int64_t,             "statuses_count">        statuses_count;
    J<string,              "created_at">    created_at;
    J<string,              "profile_banner_url">    profile_banner_url;
    J<string,              "profile_image_url_https">    profile_image_url_https;
    J<bool,                "default_profile">        default_profile;
    J<bool,                "default_profile_image">        default_profile_image;
    J<list<J<string>>,                "withheld_in_countries">        withheld_in_countries;
    J<string,              "withheld_scope">    withheld_scope;

};

struct Coordinates {
    J<int64_t,             "id">        id;

};

struct Places {
    J<int64_t,             "id">        id;

};

struct Entities {
    J<int64_t,             "id">        id;

};

struct ExtendedEntities {
    J<int64_t,             "id">        id;

};
struct Tweet {
    J<string,              "created_at"> created_at;
    J<int64_t,             "id">        id;
    J<string,              "id_str">    id_str;
    J<string,              "text">        text;
    J<string,              "source">        source;
    J<bool,                "truncated">        truncated;
    J<int64_t,             "in_reply_to_status_id">        in_reply_to_status_id;
    J<string,              "in_reply_to_status_id_str">        in_reply_to_status_id_str;
    J<int64_t,             "in_reply_to_user_id">        in_reply_to_user_id;
    J<string,              "in_reply_to_user_id_str">        in_reply_to_user_id_str;
    J<string,              "in_reply_to_screen_name">        in_reply_to_screen_name;
    J<UserObject,          "user">        user;
    J<Coordinates,         "coordinates">        coordinates;
    J<Places,              "place">        place;
    J<int64_t,             "quoted_status_id">        quoted_status_id;
    J<string,              "quoted_status_id_str">        quoted_status_id_str;
    J<bool,                "is_quote_status">        is_quote_status;
//    J<Tweet,               "quoted_status">        quoted_status;
//    J<Tweet,               "retweeted_status">        retweeted_status;
    J<int64_t,             "quote_count">        quote_count;
    J<int64_t,             "reply_count">        reply_count;
    J<int64_t,             "retweet_count">        retweet_count;
    J<int64_t,             "favorite_count">        favorite_count;
    J<Entities,            "entities">        entities;
    J<ExtendedEntities,    "extended_entities">        extended_entities;
    J<bool,                "favorited">        favorited;
    J<bool,                "retweeted">        retweeted;
    J<bool,                "possibly_sensitive">        possibly_sensitive;
    J<string,              "filter_level">        filter_level;
    J<string,              "lang">        lang;

};

struct Root_ {
    J<list<J<Tweet>>, "statuses"> statuses;
};

using Root = J<Root_>;
}
int twitterJsonPerfTest() {
    std::ifstream ifs("../../twitter.json");
    string inp = string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());

    std::cout << "inp.size " << inp.size() << std::endl;
    char * b = &inp.data()[0];
    char * e = &inp.data()[inp.size()];
    Twi::Root root;
    bool res;

    res = root.Deserialize(b, e);
    if(!res)
        throw 1;
    std::cout << root.statuses.size() << std::endl;
    std::cout <<  string(root.statuses.front().text)<< std::endl;



    doPerformanceTest("twitter.json parsing", 1000, [&res, &root, &b, &e]{
        res = root.Deserialize(b, e);

        if(!res) throw 1;
    });


    return 0;
}
