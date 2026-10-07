#include "model/Json.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace model
{

namespace
{

const JsonValue kNull;

class Parser
{
public:
	Parser(const char *text, size_t length) : p_(text), end_(text + length) {}

	JsonValue parse()
	{
		JsonValue value = parseValue(0);
		skipSpace();
		if(p_ != end_){
			fail("trailing characters");
		}
		return value;
	}

private:
	[[noreturn]] void fail(const char *message) const { throw std::runtime_error(message); }

	void skipSpace()
	{
		while(p_ != end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')){
			++p_;
		}
	}

	bool consume(const char *word)
	{
		const size_t n = std::strlen(word);
		if(static_cast<size_t>(end_ - p_) >= n && std::memcmp(p_, word, n) == 0){
			p_ += n;
			return true;
		}
		return false;
	}

	JsonValue parseValue(int depth)
	{
		if(depth > 64){
			fail("nesting too deep");
		}
		skipSpace();
		if(p_ == end_){
			fail("unexpected end");
		}
		JsonValue value;
		switch(*p_){
		case '{':
			++p_;
			value.type = JsonValue::Type::Object;
			skipSpace();
			if(p_ != end_ && *p_ == '}'){
				++p_;
				return value;
			}
			for(;;){
				skipSpace();
				if(p_ == end_ || *p_ != '"'){
					fail("object key expected");
				}
				std::string key = parseString();
				skipSpace();
				if(p_ == end_ || *p_ != ':'){
					fail("':' expected");
				}
				++p_;
				value.object.emplace_back(std::move(key), parseValue(depth + 1));
				skipSpace();
				if(p_ != end_ && *p_ == ','){
					++p_;
					continue;
				}
				if(p_ != end_ && *p_ == '}'){
					++p_;
					return value;
				}
				fail("',' or '}' expected");
			}
		case '[':
			++p_;
			value.type = JsonValue::Type::Array;
			skipSpace();
			if(p_ != end_ && *p_ == ']'){
				++p_;
				return value;
			}
			for(;;){
				value.array.push_back(parseValue(depth + 1));
				skipSpace();
				if(p_ != end_ && *p_ == ','){
					++p_;
					continue;
				}
				if(p_ != end_ && *p_ == ']'){
					++p_;
					return value;
				}
				fail("',' or ']' expected");
			}
		case '"':
			value.type = JsonValue::Type::String;
			value.string = parseString();
			return value;
		case 't':
			if(!consume("true")){ fail("bad literal"); }
			value.type = JsonValue::Type::Bool;
			value.boolean = true;
			return value;
		case 'f':
			if(!consume("false")){ fail("bad literal"); }
			value.type = JsonValue::Type::Bool;
			return value;
		case 'n':
			if(!consume("null")){ fail("bad literal"); }
			return value;
		default:
			return parseNumber();
		}
	}

	JsonValue parseNumber()
	{
		const char *start = p_;
		if(p_ != end_ && *p_ == '-'){ ++p_; }
		while(p_ != end_ && ((*p_ >= '0' && *p_ <= '9') || *p_ == '.' || *p_ == 'e' || *p_ == 'E' || *p_ == '+' || *p_ == '-')){
			++p_;
		}
		if(p_ == start){
			fail("number expected");
		}
		const std::string text(start, p_);
		JsonValue value;
		value.type = JsonValue::Type::Number;
		value.number = std::strtod(text.c_str(), nullptr);
		return value;
	}

	static void appendUtf8(std::string &out, unsigned codepoint)
	{
		if(codepoint < 0x80){
			out += static_cast<char>(codepoint);
		}
		else if(codepoint < 0x800){
			out += static_cast<char>(0xC0 | (codepoint >> 6));
			out += static_cast<char>(0x80 | (codepoint & 0x3F));
		}
		else if(codepoint < 0x10000){
			out += static_cast<char>(0xE0 | (codepoint >> 12));
			out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (codepoint & 0x3F));
		}
		else{
			out += static_cast<char>(0xF0 | (codepoint >> 18));
			out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (codepoint & 0x3F));
		}
	}

	unsigned parseHex4()
	{
		if(end_ - p_ < 4){
			fail("bad \\u escape");
		}
		unsigned value = 0;
		for(int i = 0; i < 4; ++i){
			const char c = *p_++;
			value <<= 4;
			if(c >= '0' && c <= '9'){ value |= static_cast<unsigned>(c - '0'); }
			else if(c >= 'a' && c <= 'f'){ value |= static_cast<unsigned>(c - 'a' + 10); }
			else if(c >= 'A' && c <= 'F'){ value |= static_cast<unsigned>(c - 'A' + 10); }
			else{ fail("bad \\u escape"); }
		}
		return value;
	}

	std::string parseString()
	{
		++p_; // 開きの"
		std::string out;
		while(p_ != end_ && *p_ != '"'){
			char c = *p_++;
			if(c != '\\'){
				out += c;
				continue;
			}
			if(p_ == end_){
				fail("bad escape");
			}
			c = *p_++;
			switch(c){
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u':{
				unsigned cp = parseHex4();
				if(cp >= 0xD800 && cp < 0xDC00 && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u'){ // サロゲートペア
					p_ += 2;
					const unsigned low = parseHex4();
					cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
				}
				appendUtf8(out, cp);
				break;
			}
			default: fail("bad escape");
			}
		}
		if(p_ == end_){
			fail("unterminated string");
		}
		++p_; // 閉じの"
		return out;
	}

	const char *p_;
	const char *end_;
};

} // namespace

const JsonValue &JsonValue::operator[](const char *key) const
{
	if(type == Type::Object){
		for(const auto &entry : object){
			if(entry.first == key){
				return entry.second;
			}
		}
	}
	return kNull;
}

const JsonValue &JsonValue::operator[](size_t index) const
{
	return type == Type::Array && index < array.size() ? array[index] : kNull;
}

bool parseJson(const char *text, size_t length, JsonValue &out, std::string &error)
{
	try{
		Parser parser(text, length);
		out = parser.parse();
		return true;
	}
	catch(const std::exception &e){
		error = e.what();
		return false;
	}
}

} // namespace model
