/* Stolen and modified from http://users.softlab.ece.ntua.gr/~ttsiod/buildWikipediaOffline.html
 *
 */

#include <xapian.h>
#include <iostream>
#include <string>
#include <list>
#include <cctype>
#include <algorithm>
#include <cstring>
#include <cstdio>

#include "wkindex.h"

using namespace std;

class WikiIndex {
	public:
		WikiIndex(char *db);
		~WikiIndex();

		void add_article(struct wk_page_entry_t *page);
	protected:
		Xapian::WritableDatabase database;
};

class WikiReader {
	public:
		WikiReader(char *db);
		~WikiReader();

		struct wk_title_match_t *match_query(char *pattern, int limit);
		struct wk_title_match_t *match_title(char *pattern);
	protected:
		Xapian::Database database;
};


int wkindex_init(struct wkindex_t *_x, char *db) {
	WikiIndex *x = new WikiIndex(db);
	_x->obj = static_cast<void *>(x); // i hate c++, dunno what this really does

	return 0;
}

int wkindex_destroy(struct wkindex_t *_x) {
	WikiIndex *x = static_cast<WikiIndex *>(_x->obj);
	delete x;

	_x->obj = NULL;

	return 0;
}


void wkindex_add_page(struct wkindex_t *_x, struct wk_page_entry_t *page) {
	WikiIndex *x = static_cast<WikiIndex *>(_x->obj);
	x->add_article(page);
}

int wkreader_init(struct wkreader_t *_x, char *db) {
	WikiReader *x = new WikiReader(db);
	_x->obj = static_cast<void *>(x);

	return 0;
}

int wkreader_destroy(struct wkreader_t *_x) {
	WikiReader *x = static_cast<WikiReader*>(_x->obj);
	delete x;

	_x->obj = NULL;
	return 0;
}

struct wk_title_match_t *wkreader_match_query(struct wkreader_t *_x, char *query, int limit) {
	WikiReader *x = static_cast<WikiReader*>(_x->obj);

	return x->match_query(query, limit);
}

struct wk_title_match_t *wkreader_match_title(struct wkreader_t *_x, char *title) {
	WikiReader *x = static_cast<WikiReader*>(_x->obj);

	return x->match_title(title);
}

#define DELIMS " ,.;-_!?/[]()"
inline bool is_splittable(const char c) {
	const char *x = strchr(DELIMS, c);
	return (x != NULL);
};

void trim(std::string& str)
{
	size_t pos;

	/* rtrim */
	pos = str.find_last_not_of(DELIMS);
	if (pos != string::npos)
		str.erase(pos + 1);
	else
		str.clear();

	/* ltrim */
	pos = str.find_first_not_of(DELIMS);
	if (pos != string::npos)
		str.erase(0, pos);
	else
		str.clear();
}

void wiki_tokenize(const string _str, list<string>& tokens) {
	const char *orig = _str.c_str();
	const char *str = orig;

    for (;;) {
        const char *begin = str;

		for (;;) {
			if (not *str) break;
			str++;

			if (is_splittable(*str)) break;
			if ((str > orig) &&
				isupper(str[0]) &&
				islower(str[-1]))
					break;
		}

		string token(begin, str);

		/* convert to lower */
		trim(token);

		string lower_token = token;;
		transform(token.begin(), token.end(), lower_token.begin(), ::tolower);

		if (not lower_token.empty())
			tokens.push_back(lower_token);

		if (lower_token != token) 
			tokens.push_back(token);


        if (str[0] == 0)
			break;
    }
}

/*
void tokenize(string str, list<string> tokens) {
	char s[1024]; // @TODO should be enough
	strncat(s, str.c_str(), 1000);

	char *saveptr;
	char *token = strtok_r(s, DELIMS, &saveptr);
	while (token) {
		tokens.push_back(token);
		token = strtok_r(NULL, DELIMS, &saveptr);
	}
}; */

WikiIndex::WikiIndex(char *fdb) : database(fdb, Xapian::DB_CREATE_OR_OPEN) {};
WikiIndex::~WikiIndex() {};

struct wk_title_match_t *wk_title_match_create_list(const Xapian::MSet &matches);

void WikiIndex::add_article(struct wk_page_entry_t *page) {
	Xapian::Document doc;
	doc.set_data(string(page->id));

	char buf[128];;
	doc.add_value(1, string(page->fn));

	sprintf(buf, "%lu", page->bit_offset);
	doc.add_value(2, string(buf));
	sprintf(buf, "%lu", page->byte_offset);
	doc.add_value(3, string(buf));
	sprintf(buf, "%lu", page->byte_count);
	doc.add_value(4, string(buf));


	doc.add_value(10, string(page->title));

	string title = page->title;
    doc.add_posting(title, 100); /* article title */

	string lower_title = title;
	transform(title.begin(), title.end(), lower_title.begin(), ::tolower);
	doc.add_posting(lower_title, 101);

	list<string> tokens;
	wiki_tokenize(title, tokens);

	int pos = 1000;
//	fprintf(stderr, "page: %s\n", string(page->title).c_str());

	for (list<string>::iterator iter = tokens.begin(); iter != tokens.end(); iter++) {
		//fprintf(stderr, "token[%d]: %s\n", pos, iter->c_str());
		doc.add_posting(*iter, pos++);
	};

    this->database.add_document(doc);
};

WikiReader::WikiReader(char *fdb) : database(fdb) {};
WikiReader::~WikiReader() {};

struct wk_title_match_t *WikiReader::match_query(char *query_str, int limit) {
	Xapian::Enquire enquire(this->database);
	Xapian::QueryParser parser;

	Xapian::Query query = parser.parse_query(query_str);

	enquire.set_query(query);
	Xapian::MSet matches = enquire.get_mset(0, limit);

	struct wk_title_match_t *root = wk_title_match_create_list(matches);
	return root;
}

struct wk_title_match_t *WikiReader::match_title(char *query_str) {

	// Start an enquire session
	Xapian::Enquire enquire(this->database);
	enquire.set_query(Xapian::Query(query_str));

	// Get the top 10 results of the query
	Xapian::MSet matches = enquire.get_mset(0, 25);

	struct wk_title_match_t *root = wk_title_match_create_list(matches);
	return root;
}

struct wk_title_match_t *wk_title_match_create_list(const Xapian::MSet &matches) {
	struct wk_title_match_t *root = NULL;
	struct wk_title_match_t *prev_el = NULL;
	
	for (Xapian::MSetIterator i = matches.begin(); i != matches.end(); ++i) {
		struct wk_title_match_t *next_el = (struct wk_title_match_t *)
			malloc(sizeof(struct wk_title_match_t));

		// init next */
		next_el->next = NULL;
		//next_el->id = NULL;
		//next_el->title = NULL;

		// our data
        Xapian::Document doc = i.get_document();

		fprintf(stderr, "%02d [%s] %s\n", i.get_percent(), doc.get_data().c_str(), doc.get_value(10).c_str());

		// required stuff */
		if (!prev_el) {
			prev_el = next_el;
			root = next_el;
		} else {
			prev_el->next = next_el;
			prev_el = next_el;
		}
	}

	return root;
}

void wk_title_match_free(struct wk_title_match_t *f) {
	while (f) {
		struct wk_title_match_t *next = f->next;

		free(f);
		f = next;
	}
}
