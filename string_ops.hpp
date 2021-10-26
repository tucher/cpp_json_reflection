#include <iterator>

namespace JSONReflection {

template<typename InpIter>
concept InputIteratorConcept =  std::contiguous_iterator<InpIter>
        && (
            std::same_as<typename std::iterator_traits<InpIter>::value_type, char>
            );

template <typename T>
concept SerializerOutputCallbackConcept = requires (T clb) {
    {clb(std::declval<const char*>(), std::declval<std::size_t>())} -> std::convertible_to<bool>;
};



struct DeserializationResult {
public:
    enum ErrorT {
        NO_ERROR,
        UNEXPECTED_END_OF_DATA,
        UNEXPECTED_SYMBOL,
        FIXED_SIZE_CONTAINER_OVERFLOW,
        ILLFORMED_NUMBER,
        CUSTOM_MAPPER_ERROR,
        INTERNAL_ERROR,
        SKIPPING_MAX_RECURSION,
        SKIPPING_ERROR
    };
private:
    ErrorT error = NO_ERROR;
    std::size_t offsetFromEnd = 0;
    std::size_t totalSize = 0;
public:
    DeserializationResult(std::size_t s) {
        totalSize = s;
    }
    void setError(ErrorT err, std::size_t offs) {
        error = err;
        offsetFromEnd = offs;
    }
    operator bool() {
        return error == NO_ERROR;
    }

    std::size_t getErrorOffset() {
        return totalSize-offsetFromEnd;
    }
};

namespace d {

template<class ClbT> requires  SerializerOutputCallbackConcept<ClbT>
bool outputEscapedString(const char *data, std::size_t size, ClbT && clb) {
    const char *segStart = data;
    std::size_t segSize = 0;
    std::size_t counter = 0;
    char toOutEscaped [2] = {'\\', ' '};
    while(counter < size) {
        switch(segStart[segSize]) {
            case '"':
            case '/':
            case '\\':
            case '\b':
            case '\f':
            case '\r':
            case '\n':
            case '\t':
        {

            switch(segStart[segSize]) {
            case '"':  toOutEscaped[1] = '"';  break;
            case '/':  toOutEscaped[1] = '/';  break;
            case '\\': toOutEscaped[1] = '\\'; break;
            case '\b': toOutEscaped[1] = 'b';  break;
            case '\f': toOutEscaped[1] = 'f';  break;
            case '\r': toOutEscaped[1] = 'r';  break;
            case '\n': toOutEscaped[1] = 'n';  break;
            case '\t': toOutEscaped[1] = 't';  break;
            default:
                toOutEscaped[1] == 0;
            }

            if(segSize > 0) {
                if(!clb(segStart, segSize))  [[unlikely]] {
                    return false;
                }
            }
            if(toOutEscaped[1] != 0) [[likely]] {
                if(!clb(toOutEscaped, 2))  [[unlikely]] {
                    return false;
                }
            }
            segSize = 0;
            segStart = data + counter + 1;
            break;
        }
        default:
        {
            if(std::uint8_t(segStart[segSize]) < 32) [[unlikely]] {
                return false;
            }
            segSize ++;
            break;
        }
        }
        counter++;
    }
    if(segSize > 0) {
        if(!clb(segStart, segSize))  [[unlikely]] {
            return false;
        }
    }

    return true;
}

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
bool skipWhiteSpace(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end, DeserializationResult & ctx) {
    while(isSpace(*begin)) {
        begin ++;
        if(begin == end) [[unlikely]] {
            break;
        }
    }
    if (begin == end) [[unlikely]] {
        ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
        return false;
    }
        return true;
}

bool skipTillPlainEnd(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end, DeserializationResult & ctx) {
    while(!isPlainEnd(*begin)) {
        begin ++;
        if(begin == end) [[unlikely]] {
            break;
        }
    }
    if (begin == end) [[unlikely]] {
        ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
        return false;
    }
        return true;
}



bool skipWhiteSpaceTill(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end, char s, DeserializationResult & ctx) {
    if(!skipWhiteSpace(begin, end, ctx)) [[unlikely]]{
        return false;
    }
    if(*begin == s) {
        begin ++;
        if (begin == end) [[unlikely]] {
            ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
            return false;
        }
            return true;
    }
    ctx.setError(DeserializationResult::UNEXPECTED_SYMBOL, end - begin);
    return false;
}

template<class InpIter> requires InputIteratorConcept<InpIter>
InpIter findJsonStringEnd(InpIter begin, const InpIter & end, DeserializationResult & ctx) {
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
                ctx.setError(DeserializationResult::UNEXPECTED_SYMBOL, end - begin);
                return end;
            }
        }
    }
    ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
    return end;
}

static inline bool is_integer(char c) {
  return (c >= '0' && c <= '9');
}
bool skipDouble(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end, DeserializationResult & ctx) {
   if(*begin == '-') {
       begin ++;
       if (begin == end) [[unlikely]] {
           ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
           return false;
       }
   }
   if(!is_integer(*begin)) {
       ctx.setError(DeserializationResult::UNEXPECTED_SYMBOL, end - begin);
       return false;
   }

   if(*begin != '0') {
       while(is_integer(*begin)) {
           begin ++;
           if (begin == end) [[unlikely]] {
               ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
               return false;
           }
       }
   } else {
       begin ++;
       if (begin == end) [[unlikely]] {
           ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
           return false;
       }
   }
   if(*begin == '.') {
       begin ++;
       if (begin == end) [[unlikely]] {
           ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
           return false;
       }
       if(!is_integer(*begin)) {
           ctx.setError(DeserializationResult::UNEXPECTED_SYMBOL, end - begin);
           return false;
       }
       while(is_integer(*begin)) {
           begin ++;
           if (begin == end) [[unlikely]] {
               ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
               return false;
           }
       }
   }
   if(*begin == 'e'||*begin == 'E') {
       begin ++;
       if (begin == end) [[unlikely]] {
           ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
           return false;
       }
       if(*begin == '+'||*begin == '-') {
           begin ++;
           if (begin == end) [[unlikely]] {
               ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
               return false;
           }
       }
       while(is_integer(*begin)) {
           begin ++;
           if (begin == end) [[unlikely]] {
               ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
               return false;
           }
       }
   }
   if(!skipWhiteSpace(begin, end, ctx))
       return false;
   if(!isPlainEnd(*begin))  [[unlikely]] {
       ctx.setError(DeserializationResult::UNEXPECTED_SYMBOL, end - begin);
       return false;
   }
   return true;
}
bool skipJsonValue(std::uint8_t &recursionLevelRemains, InputIteratorConcept auto & begin, const InputIteratorConcept auto & end, DeserializationResult & ctx);

template<class InpIter> requires InputIteratorConcept<InpIter>
bool skipPlainJsonValue(InpIter & begin, const InpIter & end, DeserializationResult & ctx) {
    switch(*begin) {
    case '"':
    {
        begin ++;
        if(begin == end) [[unlikely]] {
            return false;
        }
            InpIter keyBegin = begin;
        InpIter keyEnd = d::findJsonStringEnd(keyBegin, end, ctx);
        if(keyEnd == end) [[unlikely]] {
            return false;
        }
        begin = keyEnd;
        begin ++;
        if(begin == end) [[unlikely]] {
            return false;
        }
        return true;
    }
        break;
    case 't':
    {
        if(end-begin>=5 && *(begin+0) == 't'&&*(begin+1) == 'r'&&*(begin+2) == 'u'&&*(begin+3) == 'e'&&d::isPlainEnd(*(begin+4))) {
            begin += 4;
            return true;
        }
        break;
    }
        break;
    case 'f':
    {
        if(end-begin>=6 && *(begin+0) == 'f'&&*(begin+1) == 'a'&&*(begin+2) == 'l'&&*(begin+3) == 's'&&*(begin+4) == 'e'&&d::isPlainEnd(*(begin+5))) {
            begin += 5;
            return true;
        }
        break;
    }
        break;
    case 'n':
        if(end-begin>=5 && *(begin+0) == 'n'&&*(begin+1) == 'u'&&*(begin+2) == 'l'&&*(begin+3) == 'l'&&d::isPlainEnd(*(begin+4))) {
            begin += 4;
            return true;
        }
        break;
    default:
        return skipDouble(begin, end, ctx);
    }
    ctx.setError(DeserializationResult::SKIPPING_ERROR, end - begin);
    return false;
}

bool skipArrayJsonValue(std::uint8_t & recursionLevelRemains, InputIteratorConcept auto & begin, const InputIteratorConcept auto & end, DeserializationResult & ctx) {
    if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
        return false;
    }

    if(!d::skipWhiteSpaceTill(begin, end, '[', ctx)) [[unlikely]] {
        return false;
    }

    while(begin != end) {
        if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
            return false;
        }
        if(*begin == ']') {
            begin ++;
            return true;
        }

        bool ok = skipJsonValue(recursionLevelRemains, begin, end, ctx);
        if(!ok) {
            return false;
        }

        if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
            return false;
        }
        if(*begin == ',') {
            begin ++;
        }
    }
    ctx.setError(DeserializationResult::INTERNAL_ERROR, end - begin);
    return false;
}

template<class InpIter> requires InputIteratorConcept<InpIter>
bool skipObjectJsonValue(std::uint8_t & recursionLevelRemains, InpIter & begin, const InpIter & end, DeserializationResult & ctx) {
    if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
        return false;
    }
    if(!d::skipWhiteSpaceTill(begin, end, '{', ctx)) [[unlikely]] {
        return false;
    }

    while(begin != end) {
        if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
            return false;
        }
        if(*begin == '}') {
            begin ++;
            return true;
        }
        if(!d::skipWhiteSpaceTill(begin, end, '"', ctx)) [[unlikely]] {
            return false;
        }
        InpIter keyBegin = begin;
        InpIter keyEnd = d::findJsonStringEnd(keyBegin, end, ctx);
        if(keyEnd == end) [[unlikely]] {
            return false;
        }
        begin = keyEnd;
        begin ++;
        if(begin == end) [[unlikely]] {
            ctx.setError(DeserializationResult::UNEXPECTED_END_OF_DATA, end - begin);
            return false;
        }
        if(!d::skipWhiteSpaceTill(begin, end, ':', ctx)) [[unlikely]] {
            return false;
        }
        if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
            return false;
        }

        bool ok = skipJsonValue(recursionLevelRemains, begin, end, ctx);
        if(!ok) {
            return false;
        }

        if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
            return false;
        }
        if(*begin == ',') {
            begin ++;
        }
    }
    ctx.setError(DeserializationResult::SKIPPING_ERROR, end - begin);
    return false;
}

bool skipJsonValue(std::uint8_t & recursionLevelRemains, InputIteratorConcept auto & begin, const InputIteratorConcept auto & end, DeserializationResult & ctx) {
    if(!skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
        return false;
    }
    recursionLevelRemains --;
    if(recursionLevelRemains == 0) {
        ctx.setError(DeserializationResult::SKIPPING_MAX_RECURSION, end-begin);
        return false;
    }
    bool r = false;
    switch(*begin) {
    case '[':
        r = skipArrayJsonValue(recursionLevelRemains, begin, end, ctx);
        break;
    case '{':
        r =  skipObjectJsonValue(recursionLevelRemains, begin, end, ctx);
        break;
    default:
        r =  skipPlainJsonValue(begin, end, ctx);
        break;
    }
    recursionLevelRemains ++;
    return r;
}

}
}
