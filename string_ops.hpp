namespace JSONReflection {

template<typename InpIter>
concept InputIteratorConcept =  std::forward_iterator<InpIter> && requires(InpIter inp) {
    {*inp} -> std::convertible_to<char>;
};


inline bool isSpace(char a) {
    switch(a) {
    case 0x20:
    case 0x0A:
    case 0x0D:
    case 0x09:
        return true;
    }
    return false;
}

inline bool isPlainEnd(char a) {
    switch(a) {
    case ']':
    case ',':
    case '}':
    case 0x20:
    case 0x0A:
    case 0x0D:
    case 0x09:
        return true;
    }
    return false;
}
bool skipWhiteSpace(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end) {
    while(isSpace(*begin)) {
        begin ++;
        if(begin == end) [[unlikely]] {
            break;
        }
    }
    if (begin == end) [[unlikely]] {
        return false;
    }
    return true;
}

bool skipTillPlainEnd(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end) {
    while(!isPlainEnd(*begin)) {
        begin ++;
        if(begin == end) [[unlikely]] {
            break;
        }
    }
    if (begin == end) [[unlikely]] {
        return false;
    }
    return true;
}

bool skipJsonValue(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end) {
    if(!skipWhiteSpace(begin, end)) return false;
    int level = 0;
    enum class Where {
        IN_OBJ,
        IN_ARRAY,
        IN_PLAIN
    };
    Where wh;
    if(*begin != '[' && *begin != '{') {
        return skipTillPlainEnd(begin, end);
    }

    do {
        switch(*begin) {
        case '[':
            wh = Where::IN_ARRAY;
            level ++;
            break;
        case '{':
            wh = Where::IN_OBJ;
            level ++;
            break;
        case '}':
//            if(wh != Where::IN_OBJ) return false;
            level --;
            break;
        case ']':
            level --;
            break;
        }

       begin ++;
       if(begin == end) [[unlikely]] {
           break;
       }
    } while(level != 0);

    if(begin == end)
        return false;

    return true;
}


bool skipWhiteSpaceTill(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end, char s) {
    if(!skipWhiteSpace(begin, end)) return false;
    if(*begin == s) {
        begin ++;
        if (begin == end) [[unlikely]] {
            return false;
        }
        return true;
    }
    return false;
}

template<class InpIter> requires InputIteratorConcept<InpIter>
InpIter findJsonKeyStringEnd(InpIter begin, const InpIter & end) {
    for(; begin != end; begin ++) {
        char c = reinterpret_cast<char>(*begin);
        if(c == '"') {
            return begin;
        } else if (c == '\\' && std::next(begin) != end) {
            int i;
            begin++;
            switch (*begin) {
                /* Allowed escaped symbols */
                case '\"':
                case '/':
                case '\\':
                case 'b':
                case 'f':
                case 'r':
                case 'n':
                case 't':
                    break;
            /* Allows escaped symbol \uXXXX */
                case 'u': {
                    begin++;
                    auto beforeEscapedSymbol = begin;
                    for (int i = 0; i < 4 && begin != end ; i++) {
                        /* If it isn't a hex character we have an error */
                        auto currChar = *begin;
                        if (!((currChar >= 48 && currChar <= 57) ||     /* 0-9 */
                              (currChar >= 65 && currChar <= 70) ||     /* A-F */
                              (currChar >= 97 && currChar <= 102)  )) { /* a-f */
                            return end;
                        }
                        beforeEscapedSymbol = begin;
                        begin++;
                    }
                    begin = beforeEscapedSymbol;
                    }
                    break;
            /* Unexpected symbol */
                default:
                    return end;
            }
        }
    }

    return end;
}

}
