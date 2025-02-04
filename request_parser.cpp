#include "request_parser.hpp"
#include "request.hpp"

request_parser::request_parser()
	: state_(method_start)
{
}

void request_parser::reset()
{
	state_ = method_start;
}

parse_result request_parser::consume(request& req, char input)
{
	switch (state_)
	{
	case method_start:
		if (!is_char(input) || is_ctl(input) || is_tspecial(input))
		{
			return parse_result::NO;
		}
		else
		{
			state_ = method;
			req.method.push_back(input);
			return parse_result::INDETERMINATE;
		}
	case method:
		if (input == ' ')
		{
			state_ = uri;
			return parse_result::INDETERMINATE;
		}
		else if (!is_char(input) || is_ctl(input) || is_tspecial(input))
		{
			return parse_result::NO;
		}
		else
		{
			req.method.push_back(input);
			return parse_result::INDETERMINATE;
		}
	case uri:
		if (input == ' ')
		{
			state_ = http_version_h;
			return parse_result::INDETERMINATE;
		}
		else if (is_ctl(input))
		{
			return parse_result::NO;
		}
		else
		{
			req.uri.push_back(input);
			return parse_result::INDETERMINATE;
		}
	case http_version_h:
		if (input == 'H')
		{
			state_ = http_version_t_1;
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case http_version_t_1:
		if (input == 'T')
		{
			state_ = http_version_t_2;
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case http_version_t_2:
		if (input == 'T')
		{
			state_ = http_version_p;
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case http_version_p:
		if (input == 'P')
		{
			state_ = http_version_slash;
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case http_version_slash:
		if (input == '/')
		{
			req.http_version_major = 0;
			req.http_version_minor = 0;
			state_ = http_version_major_start;
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case http_version_major_start:
		if (is_digit(input))
		{
			req.http_version_major = req.http_version_major * 10 + input - '0';
			state_ = http_version_major;
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case http_version_major:
		if (input == '.')
		{
			state_ = http_version_minor_start;
			return parse_result::INDETERMINATE;
		}
		else if (is_digit(input))
		{
			req.http_version_major = req.http_version_major * 10 + input - '0';
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case http_version_minor_start:
		if (is_digit(input))
		{
			req.http_version_minor = req.http_version_minor * 10 + input - '0';
			state_ = http_version_minor;
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case http_version_minor:
		if (input == '\r')
		{
			state_ = expecting_newline_1;
			return parse_result::INDETERMINATE;
		}
		else if (is_digit(input))
		{
			req.http_version_minor = req.http_version_minor * 10 + input - '0';
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case expecting_newline_1:
		if (input == '\n')
		{
			state_ = header_line_start;
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case header_line_start:
		if (input == '\r')
		{
			state_ = expecting_newline_3;
			return parse_result::INDETERMINATE;
		}
		else if (!req.headers.empty() && (input == ' ' || input == '\t'))
		{
			state_ = header_lws;
			return parse_result::INDETERMINATE;
		}
		else if (!is_char(input) || is_ctl(input) || is_tspecial(input))
		{
			return parse_result::NO;
		}
		else
		{
			req.headers.push_back(header());
			req.headers.back().name.push_back(input);
			state_ = header_name;
			return parse_result::INDETERMINATE;
		}
	case header_lws:
		if (input == '\r')
		{
			state_ = expecting_newline_2;
			return parse_result::INDETERMINATE;
		}
		else if (input == ' ' || input == '\t')
		{
			return parse_result::INDETERMINATE;
		}
		else if (is_ctl(input))
		{
			return parse_result::NO;
		}
		else
		{
			state_ = header_value;
			req.headers.back().value.push_back(input);
			return parse_result::INDETERMINATE;
		}
	case header_name:
		if (input == ':')
		{
			state_ = space_before_header_value;
			return parse_result::INDETERMINATE;
		}
		else if (!is_char(input) || is_ctl(input) || is_tspecial(input))
		{
			return parse_result::NO;
		}
		else
		{
			req.headers.back().name.push_back(input);
			return parse_result::INDETERMINATE;
		}
	case space_before_header_value:
		if (input == ' ')
		{
			state_ = header_value;
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case header_value:
		if (input == '\r')
		{
			state_ = expecting_newline_2;
			return parse_result::INDETERMINATE;
		}
		else if (is_ctl(input))
		{
			return parse_result::NO;
		}
		else
		{
			req.headers.back().value.push_back(input);
			return parse_result::INDETERMINATE;
		}
	case expecting_newline_2:
		if (input == '\n')
		{
			state_ = header_line_start;
			return parse_result::INDETERMINATE;
		}
		else
		{
			return parse_result::NO;
		}
	case expecting_newline_3:
		if (input == '\n')
			return parse_result::YES;
		else if (input != '\n')
			return parse_result::NO;
	default:
		return parse_result::NO;
	}
}

bool request_parser::is_char(int c)
{
	return c >= 0 && c <= 127;
}

bool request_parser::is_ctl(int c)
{
	return (c >= 0 && c <= 31) || (c == 127);
}

bool request_parser::is_tspecial(int c)
{
	switch (c)
	{
	case '(': case ')': case '<': case '>': case '@':
	case ',': case ';': case ':': case '\\': case '"':
	case '/': case '[': case ']': case '?': case '=':
	case '{': case '}': case ' ': case '\t':
		return true;
	default:
		return false;
	}
}

bool request_parser::is_digit(int c)
{
	return c >= '0' && c <= '9';
}