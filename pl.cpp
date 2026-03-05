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

enum Error_Type {
    no_closing_quote,
    no_more_input
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
                if (s.length() == 1) return false;
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
        int dot_count = 0;
        int digit_count = 0;
        for (size_t i = 0; i < s.length(); ++i) {
            char c = s[i];
            if (i == 0 && (c == '-' || c == '+')) {
                if (s.length() == 1) return false; 
                continue;
            }
            if (c == '.') {
                dot_count++;
            } else if (isdigit(c)) {
                digit_count++;
            } else {
                return false; 
            }
        }
        return dot_count == 1 && digit_count > 0;
    }

    void SortToken(string token) {
        if (token == "") {
            return;
        }

        if (token == ".") {
            tokens.push_back(Token(Dot, token));
        } else if (token == "(") {
            tokens.push_back(Token(LeftParen, token));
        } else if (token == ")") {
            tokens.push_back(Token(RightParen, token));
        } else if (token == "'") {
            tokens.push_back(Token(Quote, token));
        } else if (token[0] == ';') {
            tokens.push_back(Token(LineComment, token));
        } else if (token[0] == '"') {
            tokens.push_back(Token(String, token));
        } else if (token == "nil" || token == "#f") {
            tokens.push_back(Token(Nil, token));
        } else if (token == "t" || token == "#t") {
            tokens.push_back(Token(T, token));
        } else if (IsNumber(token)) {
            int num = stoi(token);
            tokens.push_back(Token(Int, num));
        } else if (IsFloat(token)) {
            float num = stof(token);
            tokens.push_back(Token(Float, num));
        } else {
            tokens.push_back(Token(Symbol, token));
        }
    }

    void CutToken() {
        string token = "";
        for (int i = 0; i < exp.length(); i++) {
            char c = exp[i];
            if (c == ' ' || c == '\t' || c == '\n' ){
                SortToken(token);
                token = "";
            } else if (c == '(' && exp[i + 1] == ')') {
                SortToken(token);
                SortToken("nil");
                token = "";
                i++;
            } else if (c == '(' || c == ')' || c == '\'') {
                SortToken(token);
                SortToken(string(1, c));
                token = "";
            } else if (c == '"') {
                SortToken(token);
                for (int j = i + 1; j < exp.length();) {
                    size_t end = exp.find('"', j);
                    if (end == string::npos) {
                        PrintError(no_closing_quote);
                        i = exp.length();
                        break;
                    }
                    if (exp[end - 1] == '\\') {
                        j = end + 1;
                        continue;
                    } else if (exp[end - 1] == '\'' && exp[end + 1] == '\'') {
                        j = end + 2;
                        continue;
                    } else {
                        token += exp.substr(i, end - i + 1);
                        i = end;
                        break;
                    }
                    j++;
                }
                for (int j = 0; j < token.length(); j++) {
                    if (token[j] == '\\') {
                        if (j + 1 < token.length()) {
                            if (token[j + 1] == 'n') {
                                token.replace(j, 2, "\n");
                            } else if (token[j + 1] == 't') {
                                token.replace(j, 2, "\t");
                            } else if (token[j + 1] == '\\') {
                                token.replace(j, 2, "\\");
                            } else if (token[j + 1] == '"') {
                                token.replace(j, 2, "\"");
                            }
                        }
                    }
                }
                SortToken(token);
                token = "";
            } else if (c == ';') {
                SortToken(token);
                token = exp.substr(exp.find(";"));
                SortToken(token);
                token = "";
                break;
            } else {
                token += c;
            }
        }
        if (token != "") {
            SortToken(token);
        }
    }

    void PrintTestEXP() {
        for (Token t : tokens) {
            if (t.type == Symbol) {
                cout << "Symbol: " << get<string>(t.value) << endl;
            } else if (t.type == Int) {
                cout << "Int: " << get<int>(t.value) << endl;
            } else if (t.type == Float) {
                cout << "Float: " << get<float>(t.value) << endl;
            } else if (t.type == String) {
                cout << "String: " << get<string>(t.value) << endl;
            } else if (t.type == Nil) {
                cout << "Nil: " << get<string>(t.value) << endl;
            } else if (t.type == T) {
                cout << "T: " << get<string>(t.value) << endl;
            } else if (t.type == LeftParen) {
                cout << "LeftParen: " << get<string>(t.value) << endl;
            } else if (t.type == RightParen) {
                cout << "RightParen: " << get<string>(t.value) << endl;
            } else if (t.type == Dot) {
                cout << "Dot: " << get<string>(t.value) << endl;
            } else if (t.type == Quote) {
                cout << "Quote: " << get<string>(t.value) << endl;
            } else if (t.type == LineComment) {
                cout << "LineComment: " << get<string>(t.value) << endl;
            } else {
                cout << "Unknown type" << endl;
            }
        }
    }

    void PrintEXP() {
        for (Token t : tokens) {
            if (t.type == Symbol) {
                cout << endl << "> ";
                cout << get<string>(t.value) << endl;
            } else if (t.type == Int) {
                cout << endl << "> ";
                cout << get<int>(t.value) << endl;
            } else if (t.type == Float) {
                cout << endl << "> ";
                printf("%.3f\n", get<float>(t.value));
            } else if (t.type == String) {
                cout << endl << "> ";
                cout << get<string>(t.value) << endl;
            } else if (t.type == Nil) {
                cout << endl << "> ";
                cout << "nil" << endl;
            } else if (t.type == T) {
                cout << endl << "> ";
                cout << "#t" << endl;
            } else if (t.type == LeftParen) {
                //cout << get<string>(t.value) << endl;
            } else if (t.type == RightParen) {
                //cout << get<string>(t.value) << endl;
            } else if (t.type == Dot) {
                cout << endl << "> ";
                cout << get<string>(t.value) << endl;
            } else if (t.type == Quote) {
                cout << endl << "> ";
                cout << get<string>(t.value) << endl;
            } else if (t.type == LineComment) {
                //cout << get<string>(t.value) << endl;
            } else {
                cout << "Unknown type" << endl;
            }
        }
    }

    void PrintError (Error_Type error) {
        if (error == no_closing_quote) {
            cout << endl << "> ERROR (no closing quote) : END-OF-LINE encountered at Line 3 Column 56" << endl;
        } else if (error == no_more_input) {
            cout << endl << "> ERROR (no more input) : END-OF-FILE encountered";
        } else {
            cout << "Unknown error" << endl;
        }
    }
public:
    PL() {
        exp = "";
    }

    ~PL() {
        exp = "";
    }

    void Reset() {
        exp = "";
        tokens.clear();
    }

    void Process(int i) {
        CutToken();
        if (i != 0) {
            PrintEXP();
        }
        Reset();
    }

    bool ReadEXP() {
        if (!getline(cin, exp) ) {
            PrintError(no_more_input);
            return false;
        } else if (exp == "(exit)") {
            cout << endl << "> ";
            return false;
        }
        return true;
    }

};



int main() {
    PL pl;
    cout << "Welcome to OurScheme!" << endl;
    for (int i = 0;; i++) {
        if (!pl.ReadEXP()) break;
        pl.Process(i);
    }
    cout << endl << "Thanks for using OurScheme!" << endl;
    return 0;
}