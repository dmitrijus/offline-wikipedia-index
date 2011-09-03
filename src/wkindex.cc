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

#include "wkindex.h"

using namespace std;

class WikiIndex {
	public:
		WikiIndex(char *db);
		~WikiIndex();

		void add_article(char *title, struct page_info_t *page);
	protected:
		Xapian::WritableDatabase database;
};

void wkindex_init(struct wkindex_t *_x, char *db) {
	WikiIndex *x = new WikiIndex(db);
	_x->obj = static_cast<void *>(x); // i hate c++, dunno what this really does
};

void wkindex_destroy(struct wkindex_t *_x) {
	WikiIndex *x = static_cast<WikiIndex *>(_x->obj);
	delete x;
	
	_x->obj = NULL;
};

void wkindex_add_page(struct wkindex_t *_x, char *title, struct page_info_t *page) {
	WikiIndex *x = static_cast<WikiIndex *>(_x->obj);
	x->add_article(title, page);
};

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
		transform(token.begin(), token.end(), token.begin(), ::tolower);
		trim(token);

		if (not token.empty())
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

void WikiIndex::add_article(char *source, struct page_info_t *page) {
	Xapian::Document doc;
	doc.set_data(source);

	string title = page->title;
    doc.add_posting(title, 1); /* article title */

	string lower_title = title;
	transform(title.begin(), title.end(), lower_title.begin(), ::tolower);
	doc.add_posting(lower_title, 2);

	list<string> tokens;
	wiki_tokenize(title, tokens);

	int pos = 10;
	//fprintf(stderr, "page: %s\n", title.c_str());

	for (list<string>::iterator iter = tokens.begin(); iter != tokens.end(); iter++) {
		//fprintf(stderr, "token[%d]: %s\n", pos, iter->c_str());
		doc.add_posting(*iter, pos++);
	};

    this->database.add_document(doc);
};

/*
int main(int argc, char **argv)
{
    unsigned total = 0;
    try {
        // Make the database
        Xapian::WritableDatabase database("db/", Xapian::DB_CREATE_OR_OPEN);

        string docId;
        while(1) {
            string title;
            if (cin.eof()) break;
            getline(cin, title);
            int l = title.length();
            if (l>4 && title[0] == '#' && title.substr(l-4, 4) == ".bz2") {
                docId = title.substr(1, string::npos);  
                continue;
            }

            string Title = title;
            lowcase(title);

            // Make the document
            Xapian::Document newdocument;

            // Target: filename and the exact title used
            string target = docId + string(":") + Title;
            if (target.length()>MAX_KEY)
                target = target.substr(0, MAX_KEY);
            newdocument.set_data(target);

            // 1st Source: the lowercased title
            if (title.length() > MAX_KEY)
                title = title.substr(0, MAX_KEY);
            newdocument.add_posting(title.c_str(), 1);

            vector<string> keywords;
            Tokenize(title, keywords, " ");

            // 2nd source: All the title's lowercased words
            int cnt = 2;
            for (vector<string>::iterator it=keywords.begin(); 
                it!=keywords.end(); it++) 
            {
                if (it->length() > MAX_KEY)
                    *it = it->substr(0, MAX_KEY);
                newdocument.add_posting(it->c_str(), cnt++);
            }

            try {
                //cout << "Added " << title << endl;
                // Add the document to the database
                database.add_document(newdocument);
            } catch(const Xapian::Error &error) {
                cout << "Exception: "  << error.get_msg();
                cout << "\nWhen adding:\n" << title;
                cout << "\nOf length " << title.length() << endl;
            }
            total ++;

            if ((total % 8192) == 0) {
                cout << total << " articles indexed so far" << endl;
            }
        }
    } catch(const Xapian::Error &error) {
        cout << "Exception: "  << error.get_msg() << endl;
    }
    cout << total << " articles indexed." << endl;
}
*/

