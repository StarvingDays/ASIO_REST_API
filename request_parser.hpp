#pragma once

#include <tuple>

enum class parse_result
{
	YES,
	NO,
	INDETERMINATE
};

struct request;

/// Parser for incoming requests.
class request_parser
{
public:
	/// Construct ready to parse the request method.
	request_parser();
	request_parser(const request_parser&) = delete;
	request_parser(const request_parser&&) = delete;
	request_parser& operator=(const request_parser&) = delete;
	request_parser& operator=(const request_parser&&) = delete;

	/// Reset to initial parser state.
	void reset();

	/// Parse some data. The tribool return value is true when a complete request
	/// has been parsed, false if the data is invalid, indeterminate when more
	/// data is required. The InputIterator return value indicates how much of the
	/// input has been consumed.

	template <typename InputIterator>
	std::tuple<parse_result, InputIterator> parse(request& req, InputIterator begin, InputIterator end)
	{
		auto state = this->state_;
		while (begin != end)
		{
			parse_result result = consume(req, *begin++);
			if ((result == parse_result::YES) || (result == parse_result::NO))
				return std::make_tuple(result, begin);
		}
		parse_result result = parse_result::INDETERMINATE;
		return std::make_tuple(result, begin);
	}

private:
	/// Handle the next character of input.
	parse_result consume(request& req, char input);

	/// Check if a byte is an HTTP character.
	static bool is_char(int c);

	/// Check if a byte is an HTTP control character.
	static bool is_ctl(int c);

	/// Check if a byte is defined as an HTTP tspecial character.
	static bool is_tspecial(int c);

	/// Check if a byte is a digit.
	static bool is_digit(int c);

	/// The current state of the parser.
	enum state
	{
		method_start,
		method,
		uri,
		http_version_h,
		http_version_t_1,
		http_version_t_2,
		http_version_p,
		http_version_slash,
		http_version_major_start,
		http_version_major,
		http_version_minor_start,
		http_version_minor,
		expecting_newline_1,
		header_line_start,
		header_lws,
		header_name,
		space_before_header_value,
		header_value,
		expecting_newline_2,
		expecting_newline_3
	} state_;
};