#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <variant>
using namespace std;

enum Token_Type {
    Symbol,
    Int,
    Float,
    String,
    Nil,
    T,
    LeftParen,
    RightParen,
    Dot,
    Quote,
    LineComment
};

struct Token {
    Token_Type type;
    variant<int, float, string> value;
    
    Token(Token_Type t, variant<int, float, string> v) : type(t), value(v) {}
};


class PL {
private:
    string exp;
    vector<Token> tokens;

    bool IsNumber (string s) {
        if (s.empty()) {
            return false;
        }
        int i = 0;
        for (char c : s) {
            if (i == 0 && c == '-' || i == 0 && c == '+') {
                i++;
                continue;
            }
            if (!isdigit(c)) {
                return false;
            }
            i++;
        }
        return true;
    }

    bool IsFloat (string s) {
        if (s.empty()) {
            return false;
        }
        int i = 0;
        int dot_count = 0;
        for (char c : s) {
            if (i == 0 && c == '-' || i == 0 && c == '+') {
                i++;
                continue;
            }
            if (!isdigit(c)) {
                if (c == '.') {
                    dot_count++;
                } else {
                    return false;
                }
            }
        }
        if (dot_count == 1) {
            return true;
        } else {
            return false;
        }
    }

    void SortToken(string token) {
        if (token == ".") {
            tokens.push_back(Token(Dot, token));
        } else if (token == "(") {
            tokens.push_back(Token(LeftParen, token));
        } else if (token == ")") {
            tokens.push_back(Token(RightParen, token));
        } else if (token == "'") {
            tokens.push_back(Token(Quote, token));
        } else if (token == ";") {
            tokens.push_back(Token(LineComment, token));
        } else if (token == "nil" || token == "#f") {
            tokens.push_back(Token(Nil, token));
        } else if (token == "t" || token == "#t") {
            tokens.push_back(Token(T, token));
        } else if (IsNumber(token)) {
            tokens.push_back(Token(Int, token));
        } else if (IsFloat(token)) {
            tokens.push_back(Token(Float, token));
        } else {
            tokens.push_back(Token(Symbol, token));
        }
    }

    void Tokenize() {
        string token = "";
        for (char c : exp) {
            if (c == ' ' || c == '\t' || c == '\n' ){
                if (token != "") {
                    SortToken(token);
                    token = "";
                }
            } else if (c == '"' || c == '(' || c == ')' || c == '\'') {
                SortToken(token);
                SortToken(string(1, c));
                token = "";
            } else if (c == ';') {
                SortToken(token);
                SortToken(string(1, c));
                token = exp.substr(exp.find(string(1, c)) + 1);
                break;
            } else {
                token += c;
            }
        }
        if (token != "") {
            SortToken(token);
        }
    }
public:
    PL() {
        exp = "";
    }

    ~PL() {
        exp = "";
    }

    void Process() {
        Tokenize();
    }

    bool ReadEXP() {
        if (!getline(cin, exp) || exp == "(exit)") {
            return false;
        }
        return true;
    }

    string PrintEXP() {
        cout << exp << endl;
        return exp;
    }
};



int main() {
    PL pl;
    cout << "Welcome to OurScheme!" << endl;
    while (1) {
        cout << endl << "> ";
        if (!pl.ReadEXP()) break;
        pl.Process();
        pl.PrintEXP();
    }
    cout << endl << "Thanks for using OurScheme!" << endl;
    return 0;
}