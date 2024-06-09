#include <iostream>
#include <fstream>
#include <cstring>
#include <cctype> 
#include <vector>
#include <string>
#include <algorithm>
using namespace std;

/* Define class and struct */
class Symbol {
    private:
        string symbol;
    
    public:
        Symbol(const string& val = ""): symbol(val) {}
    
    // Validate the symbols
    bool isValid() const {  
        if (!isalpha(symbol[0]) || symbol.size() > 16) {
            return false;
        }
        return true;
    }

    // Return the string value
    string getSymbol() const {
        return symbol;
    }
};

// Symbol table, module table
struct symbolData {
    Symbol symbolName;  // symbol name
    int absoluteAddr;
    bool isRedefined;  // Whether the symbol is redefined: handle Rule 5 (error)
    bool isUsed;  // Whether the symbol is used after defined
    int moduleNum;  // In which module is the symbol defined
};

struct moduleData {
    int moduleNum;  // module name
    int length;  // module length
};

// Variables for Tokenizer
int lineCnt = 0;
int tokenPos = 0;  // Initialize to track token position within every line
int offset = 0;    // To track the offset within the file or line
string file;
fstream inputFile;
// Variables for parse error
bool parseErr = false;
int ttlInstcount = 0;  // Cannot exceed machine size
// Variables for symbol & module table
int module = 0;
int moduleLength = 0;
int currRelativeAddr;
// Variables for memory map table
int currMemoryNum;
bool symbolNotFoundErr = true;  // for rule 3
Symbol undefinedSymbol;  // for rule 3
// Check whether the last line empty, leading to different tokenPos
bool lastLineEmpty = false;
int tokenLength; 

// Symbol table & Module table (Pass 1)
vector<symbolData> symbolTable;  
vector<moduleData> moduleTable;
// for Rule 7
vector<Symbol> tempUseList;
vector<bool> tempIsReferred;

/* Define functions */
// Handle Rule 2 (warning) 
bool isRedefined(Symbol currSymbol) {
    for (const symbolData& entry: symbolTable) {
        if (entry.symbolName.getSymbol() == currSymbol.getSymbol()) {
            cout << "Warning: Module " << module << ": " << currSymbol.getSymbol() << " redefinition ignored" << endl;
            return true;
        }
    }
    return false;
}

void createSymbol(Symbol currSymbol, int currRelativeAddr) {
    if (!isRedefined(currSymbol)) {
        symbolData newSymbol = {currSymbol, moduleLength + currRelativeAddr, false, false, module};  // default: symRedefined = false
        symbolTable.push_back(newSymbol);
    } else {
        // If the symbol is redefined, change the "isRedefined" column in symbolTable to "true"
        for (symbolData& entry: symbolTable) {
            if (entry.symbolName.getSymbol() == currSymbol.getSymbol()) {
                entry.isRedefined = true;
                break;
            }
        }
    }
}

// Function to print the symbol table
void printSymbolTable(const vector<symbolData>& table) { 
    cout << "Symbol Table" << endl;
    for (const symbolData& symbol: table) {  
        cout << symbol.symbolName.getSymbol() << "=" << symbol.absoluteAddr;
        if (symbol.isRedefined) {
            cout << " Error: This variable is multiple times defined; first value used";
        }
        cout << endl;
    }  
}

// Function to create memory map numbers with leading 0's
string addLeadingZeros(int memoryNum) {
    if (memoryNum >= 0 && memoryNum < 10) {
        return "00" + to_string(memoryNum);
    } else if (memoryNum >= 10 && memoryNum < 100) {
        return "0" + to_string(memoryNum);
    } else {
        return to_string(memoryNum);
    }
}

// Function to create final memory location numbers with leading 0's
string addLeadingZeros1(int globalAddr) {
    if (globalAddr >= 0 && globalAddr < 10) {
        return "000" + to_string(globalAddr);
    } else if (globalAddr >= 10 && globalAddr < 100) {
        return "00" + to_string(globalAddr);
    } else if (globalAddr >= 100 && globalAddr < 1000) {
        return "0" + to_string(globalAddr);
    } else {
        return to_string(globalAddr);
    }
}

// Tokenizer
char* getToken() {
    static string strLine;
    static char* savePtr = nullptr;
    static char* buffer = nullptr;

    if (!inputFile.is_open()) {
        inputFile.open(file);
        if (!inputFile.is_open()) {
            cout << "Error opening file" << endl;
            return nullptr;
        }
    }

    // Loop to handle continuous reading and tokenizing
    while (true) {
        if (savePtr == nullptr) {  // Attempt to read the next line from the file
            delete[] buffer; // Ensure the previous buffer is cleared
            buffer = nullptr;
            lastLineEmpty = strLine.empty();  // Detect whether the last line in the file is empty

            if (!getline(inputFile, strLine)) {  // Check for end of file or error
                if (inputFile.eof()) {
                    inputFile.close();
                    return nullptr;
                } else {
                    cout << "Error reading file" << endl;
                    return nullptr;
                }
            }

            // Replace '\t' with ' '
            replace(strLine.begin(), strLine.end(), '\t', ' ');

            // Process the new line
            lineCnt++;
            if (strLine.empty()) {
                continue; // Skip the rest of the loop and attempt to read the next line
            }

            buffer = new char[strLine.length() + 1];
            strcpy(buffer, strLine.c_str());
            savePtr = buffer;  // Initialize savePtr to point to the beginning of buffer
        }

        // tokenize the current buffer
        char* token = strtok_r(savePtr, " \t", &savePtr);
        if (token != nullptr) {
            tokenPos = token - buffer + 1;  // Update token position 
            tokenLength = strlen(token);
            offset = tokenPos + tokenLength;
            return token;
        } else {
            // No more tokens in the current line -> set savePtr to nullptr
            savePtr = nullptr; // This will cause the loop to attempt to read the next line
        }
    }
}

// Check 1: readInt() function
int readInt() {
    int num = 0;  // defcount, usecount, instcount
    char* tok = getToken();  // "tok" is a pointer (if first token is "1000", tok points to "1")
    // EOF
    if (tok == nullptr) { 
        return -1;
    }
    // Check whether the token is a number
    for (; *tok != '\0'; tok++) {  
        if (!isdigit(*tok)) {
            cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": NUM_EXPECTED" << endl;
            parseErr = true;
            return 0;
        }
        int digit = *tok - '0';  // get the integer value of the digit character
        num = num * 10 + digit;
    }
    if (num >= 1 << 30) {  // 1 << 30: 2^30
        cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": NUM_EXPECTED" << endl;
        parseErr = true;
        return 0;
    }
    return num;
}

// Check 2: readSymbol() function
Symbol readSym() {
    char* tok = getToken();  // Get the next token
    //EOF
    if (tok == nullptr) { 
        if (!lastLineEmpty) {
            cout << "Parse Error line " << lineCnt << " offset " << tokenPos + tokenLength << ": SYM_EXPECTED" << endl;
        } else {
            cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": SYM_EXPECTED" << endl;
        }
        exit(2); 
    }
    // Convert the null-terminated character array pointed by tok to a string
    string symbolStr(tok);
    // Create a smybol object with symbolStr
    Symbol symbolObj(symbolStr);
    // Check
    if (!symbolObj.isValid()) {
        if (!isalpha(symbolStr[0])) {
            cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": SYM_EXPECTED" << endl;
        } else if (symbolStr.size() > 16) {
            cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": SYM_TOO_LONG" << endl;
        }
        parseErr = true;
    }
    return symbolObj;
}

// Check 3: readMARIE() function
char readMARIE() {
    char instrCode;
    char* tok = getToken();
    // EOF
    if (tok == nullptr) { 
        if (!lastLineEmpty) {
            cout << "Parse Error line " << lineCnt << " offset " << tokenPos + tokenLength << ": MARIE_EXPECTED" << endl;
        } else {
            cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": MARIE_EXPECTED" << endl;
        }
        exit(2);  
    }
    // Check whether the token is M,A,R,I,E
    string instrStr(tok);
    if (instrStr.size() == 1) {
        char instrChar = instrStr[0];
        if (instrChar == 'M' || instrChar == 'A' || instrChar == 'I' || instrChar == 'R' || instrChar == 'E') {
            return instrChar;
        }
    } 
    cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": MARIE_EXPECTED" << endl;
    parseErr = true;
    exit(2);
}

// Pass 1
void Pass1() {
    while(true) {
        /* Group 1 */
        int defcount = readInt();
        // EOF or parse error
        if (defcount < 0 || parseErr || defcount > 16) {
            if (parseErr) {
                // Error message "NUM_EXPECTED" is already printed by readInt() above
                exit(2);
            } else if (defcount > 16) {
                cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": TOO_MANY_DEF_IN_MODULE" << endl;
                exit(2);
            } else {
                break; 
            }
        }
        for (int i = 0; i < defcount && !parseErr; i++) {
            Symbol sym = readSym();
            if (parseErr) {
                // Error message is already printed by readSym() above
                cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": SYM_EXPECTED" << endl;
                exit(2);
            }
            int val = readInt();
            if (parseErr) {
                // Error message is already printed by readInt() above
                exit(2);
            }
            createSymbol(sym, val);
        }
        /* Group 2 */
        int usecount = readInt();
        // EOF or parse error
        if (usecount < 0 || parseErr || usecount > 16) {
            if (parseErr) {
                // Error message "NUM_EXPECTED" is already printed by readInt() above
                exit(2);
            } else if (usecount > 16) {
                cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": TOO_MANY_USE_IN_MODULE" << endl;
                exit(2);
            } else {
                break;  
            }
        }
        for (int i=0;i<usecount;i++) {
            Symbol sym = readSym();
        }
        /* Group 3 */
        int instcount = readInt();
        ttlInstcount += instcount;
        // EOF or parse error
        if (instcount < 0 || parseErr || ttlInstcount > 512) {  
            if (parseErr) {
                // Error message "NUM_EXPECTED" is already printed by readInt() above
                exit(2);
            } else if (ttlInstcount > 512) {
                cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": TOO_MANY_INSTR" << endl;
                exit(2);
            } else {
                break;  
            }
        }
        // Handle Rule 5 (warning)
        for (symbolData& entry: symbolTable) {
            if (entry.absoluteAddr - moduleLength > instcount) {
                cout << "Warning: Module " << module << ": " << entry.symbolName.getSymbol() << "=" << entry.absoluteAddr - moduleLength << " valid=[0.." << to_string(instcount-1) << "] assume zero relative" << endl;
                entry.absoluteAddr = moduleLength;
            }
        }

        // Update moduleTable after knowing the length of the previous module
        module++;
        moduleLength += instcount;
        moduleData newModule = {module, moduleLength};
        moduleTable.push_back(newModule);

        for (int i = 0; i < instcount; i++) {
            char addressmode = readMARIE();
            if (parseErr) {
                // Error message is already printed by readMARIE() above
                exit(2);
            }
            int operand = readInt();
            // various checks (Pass 2)
        }
    }
}

// Reset all variables & file for Pass 2
void reset() {
    lineCnt = 0;
    tokenPos = 0;  
    offset = 0;   
    parseErr = false;
    ttlInstcount = 0;
    module = 0;
    moduleLength = 0;
    inputFile.clear();
    inputFile.seekg(0, ios::beg);
}

// Pass 2
void Pass2() {
    while (true) {
        /* Group 1 */
        int defcount = readInt();
        // EOF or parse error
        if (defcount < 0 || parseErr || defcount > 16) {
            if (parseErr) {
                exit(2);
            } else if (defcount > 16) {
                cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": TOO_MANY_DEF_IN_MODULE" << endl;
                exit(2);
            } else {
                break;  
            }
        }
        for (int i = 0; i < defcount && !parseErr; i++) {
            Symbol sym = readSym();
            if (parseErr) {
                exit(2);
            }
            int val = readInt();
            if (parseErr) {
                exit(2);
            }
        }
        /* Group 2 */
        int usecount = readInt();
        if (usecount < 0 || parseErr || usecount > 16) {
            if (parseErr) {
                exit(2);
            } else if (usecount > 16) {
                cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": TOO_MANY_USE_IN_MODULE" << endl;
                exit(2);
            } else {
                break;  
            }
        }
        // Create a temporary array to store the symbols for 'E' instruction below
        for (int i = 0; i < usecount; i++) {
            Symbol sym = readSym();
            tempUseList.push_back(sym);
            tempIsReferred.push_back(false);
            // Check whether the symbol is used (for "Warning: Module %d: %s was defined but never used")
            for (symbolData& entry: symbolTable) {
                if (entry.symbolName.getSymbol() == sym.getSymbol()) {
                    entry.isUsed = true;
                }
            }
        }
        /* Group 3 */
        int instcount = readInt();
        if (instcount < 0 || parseErr || instcount > 510) {  
            if (parseErr) {
                exit(2);
            } else if (instcount > 510) {
                cout << "Parse Error line " << lineCnt << " offset " << tokenPos << ": TOO_MANY_INSTR" << endl;
                exit(2);
            } else {
                break;  
            }
        }
        for (int i = 0; i < instcount; i++) {
            char addressmode = readMARIE();
            if (parseErr) {
                exit(2);
            }
            int operand = readInt();
            int operand_ = operand % 1000;
            string memoryNumStr = addLeadingZeros(currMemoryNum);
            // Error: Illegal opcode; treated as 9999
            if (operand/1000 <= 9) {
                // various checks
                switch (addressmode) {
                case ('M'): {
                    if (operand_ < moduleTable.size()-1) {
                        int tarModuleAddr = 0;
                        for (moduleData& entry: moduleTable) {
                            if (entry.moduleNum == operand_) {
                                tarModuleAddr = entry.length;
                            }
                        }
                        int memoryAddr = operand - operand_ + tarModuleAddr;  // Replace operand with the base address of the targeted module
                        cout << memoryNumStr << ": " << addLeadingZeros1(memoryAddr) << endl;
                    } else {
                        int memoryAddr = operand - operand_;
                        cout << memoryNumStr << ": " << addLeadingZeros1(memoryAddr) << " Error: Illegal module operand ; treated as module=0" << endl;
                    }  
                    break;
                }   
                case ('A'): {
                    if (operand_ <= 512) {
                        cout << memoryNumStr << ": " << addLeadingZeros1(operand) << endl;
                    } else {
                        cout << memoryNumStr << ": " << addLeadingZeros1(operand - operand_) << " Error: Absolute address exceeds machine size; zero used" << endl;
                    }
                    break;
                }
                case ('I'): {
                    if (operand_ < 900) {
                        cout << memoryNumStr << ": " << addLeadingZeros1(operand) << endl;
                    } else {
                        cout << memoryNumStr << ": " << addLeadingZeros1(operand - operand_ + 999) << " Error: Illegal immediate operand; treated as 999" << endl;
                    }
                    break;
                }
                case ('R'): {
                    if (operand_ <= instcount) {
                        int absoluteAddr_R = operand + moduleLength;
                        cout << memoryNumStr << ": " << addLeadingZeros1(absoluteAddr_R) << endl;
                    } else {
                        cout << memoryNumStr << ": " << addLeadingZeros1(operand - operand_ + moduleLength) << " Error: Relative address exceeds module size; relative zero used" << endl;
                    }
                    break;
                }
                case ('E'): {
                    if (operand_ < tempUseList.size()) {
                        Symbol symbolUsed = tempUseList.at(operand_);
                        // If the symbol is actually referred, mark it as true (for Rule 7)
                        tempIsReferred[operand_] = true;
                        int absoluteAddrUsed = 0;
                        symbolNotFoundErr = true;
                        for (symbolData& entry: symbolTable) {
                            if (entry.symbolName.getSymbol() == symbolUsed.getSymbol()) {
                                absoluteAddrUsed = entry.absoluteAddr;
                                symbolNotFoundErr = false;
                            }
                        }
                        if (!symbolNotFoundErr) {
                            cout << memoryNumStr << ": " << addLeadingZeros1(operand - operand_ + absoluteAddrUsed) << endl;
                        } else {
                            cout << memoryNumStr << ": " << addLeadingZeros1(operand - operand_) << " Error: " << symbolUsed.getSymbol() << " is not defined; zero used" << endl;
                            undefinedSymbol = symbolUsed;
                        }
                    } else {
                        cout << memoryNumStr << ": " << addLeadingZeros1(operand - operand_) << " Error: External operand exceeds length of uselist; treated as relative=0" << endl;
                        symbolNotFoundErr = false;  // This is not the error of undefined (rule 3)
                    }
                    break;
                }
                default:
                    break;
                }
            } else {
                cout << memoryNumStr << ": " << 9999 << " Error: Illegal opcode; treated as 9999" << endl;
            }
            currMemoryNum++;
        }
        for (int i = 0; i < tempUseList.size(); i++) {
            if (!tempIsReferred[i]) {
                cout << "Warning: Module " << module << ": uselist[" << i << "]=" << tempUseList[i].getSymbol() << " was not used" << endl;
            }
        }
        // Reset tempUseList & tempIsReferred for iteration
        tempUseList.clear();
        tempIsReferred.clear();
        // Update moduleTable after knowing the length of the previous module
        module++;
        moduleLength += instcount;
    }
    cout << endl;
    for (symbolData& entry: symbolTable) {
        if (!entry.isUsed) {
            cout << "Warning: Module " << entry.moduleNum << ": " << entry.symbolName.getSymbol() << " was defined but never used" << endl;
        }
    }
}

int main(int argc, char *argv[]) {
    // First add the base address of module 1 to the table as it starts from 0
    moduleData newModule = {module, moduleLength};
    moduleTable.push_back(newModule);
    file = argv[1];  
    Pass1();  // Call Pass1() to process the file & create symbol table
    printSymbolTable(symbolTable);
    cout << endl;
    cout << "Memory Map" << endl;
    reset();  // reset all
    Pass2(); 
    cout << endl;
    return 0;
}