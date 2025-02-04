#pragma once

#include <string>


struct mapping
{
	const char* extension;
	const char* mime_type;
};

struct reply;
struct request;

/// The common handler for all incoming requests.
class request_handler
{
public:
	request_handler() = delete;
	request_handler(const request_handler&) = delete;
	request_handler(const request_handler&&) = delete;
	request_handler& operator=(const request_handler&) = delete;
	request_handler& operator=(const request_handler&&) = delete;


	/// Construct with a directory containing files to be served.
	explicit request_handler(const std::string& doc_root);

	/// Handle a request and produce a reply.
	void handle_request(const request& req, reply& rep);

private:
	/// The directory containing the files to be served.
	std::string doc_root_;

	mapping mappings[6] = {
	{ "gif", "image/gif" },
	{ "htm", "text/html" },
	{ "html", "text/html" },
	{ "jpg", "image/jpeg" },
	{ "png", "image/png" },
	{ 0, 0 } };

	std::string extension_to_type(const std::string& extension);
	/// Perform URL-decoding on a string. Returns false if the encoding was
	/// invalid.
	static bool url_decode(const std::string& in, std::string& out);
};