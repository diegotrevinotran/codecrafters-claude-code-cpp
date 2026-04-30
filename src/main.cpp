#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * Helper method that executes the Read tool.
 */
std::string read_tool(std::string file_path) {
    std::ifstream file(file_path);
    std::string str;
    std::string file_contents;
    while (std::getline(file, str))
    {
    file_contents += str;
    file_contents.push_back('\n');
    } 
    return file_contents;
}

/**
 * Helper method that executes the Write tool.
 */
std::string write_tool(std::string file_path, std::string content) {
    std::ofstream file(file_path);
    file << content;
    return content;
}

std::string command_tool(std::string command) {
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(command, "r");

    if (!pipe) return "";
    
    try {
        while (fgets(buffer, sizeof buffer, pipe) != NULL) {
            result += buffer;
        }
    } catch (...) {
        pclose(pipe);
        return "";
    }
    pclose(pipe);
    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 3 || std::string(argv[1]) != "-p") {
        std::cerr << "Expected first argument to be '-p'" << std::endl;
        return 1;
    }

    std::string prompt = argv[2];

    if (prompt.empty()) {
        std::cerr << "Prompt must not be empty" << std::endl;
        return 1;
    }

    const char* api_key_env = std::getenv("OPENROUTER_API_KEY");
    const char* base_url_env = std::getenv("OPENROUTER_BASE_URL");

    std::string api_key = api_key_env ? api_key_env : "";
    std::string base_url = base_url_env ? base_url_env : "https://openrouter.ai/api/v1";

    if (api_key.empty()) {
        std::cerr << "OPENROUTER_API_KEY is not set" << std::endl;
        return 1;
    }

    json messages = {
                {{"role", "user"}, {"content", prompt}}
    };

    while (true) {
        json request_body = {
            {"model", "anthropic/claude-haiku-4.5"},
            {"messages", messages},
            {"tools", json::array({
                { // Read tool
                    {"type", "function"},
                    {"function", {
                        {"name", "Read"},
                        {"description", "Read and return the contents of a file"},
                        {"parameters", {
                            {"type", "object"},
                            {"properties", {
                                {"file_path", {
                                    {"type", "string"},
                                    {"description", "The path to the file to read"}
                                }}
                            }},
                            {"required", json::array({"file_path"})}
                        }}
                    }}
                },
                { // Write tool
                    {"type", "function"},
                    {"function", {
                        {"name", "Write"},
                        {"description", "Write content to a file"},
                        {"parameters", {
                            {"type", "object"},
                            {"required", json::array({"file_path", "content"})},
                            {"properties", {
                                {"file_path", {
                                    {"type", "string"},
                                    {"description", "The path of the file to write to"}
                                }},
                                {"content", {
                                    {"type", "string"},
                                    {"description", "The content to write to the file"}
                                }}
                            }}
                        }}
                    }}
                },
                {
                    {"type", "function"},
                    {"function", {
                        {"name", "Bash"},
                        {"description", "Execute a shell command"},
                        {"parameters", {
                            {"type", "object"},
                            {"required", json::array({"command"})},
                            {"properties", {
                                {"command", {
                                    {"type", "string"},
                                    {"description", "The command to execute"}
                                }}
                            }}
                        }}
                    }}
                }
            })}
        };

        cpr::Response response = cpr::Post(
            cpr::Url{base_url + "/chat/completions"},
            cpr::Header{
                {"Authorization", "Bearer " + api_key},
                {"Content-Type", "application/json"}
            },
            cpr::Body{request_body.dump()}
        );

        if (response.status_code != 200) {
            std::cerr << "HTTP error: " << response.status_code << std::endl;
            return 1;
        }

        json result = json::parse(response.text);

        if (!result.contains("choices") || result["choices"].empty()) {
            std::cerr << "No choices in response" << std::endl;
            return 1;
        }

        messages.push_back(result["choices"][0]["message"]);

        // handle tool calls
        json msg = result["choices"][0]["message"];
        if (msg.contains("tool_calls")) {
            // declare helper vars
            json tool_call = msg["tool_calls"][0];
            std::string tool_name = tool_call["function"]["name"];
            std::string output;

            if (tool_name == "Read") {
                // parse argument string and extract filepath
                json args = json::parse(tool_call["function"]["arguments"].get<std::string>());
                std::string file_path = args["file_path"].get<std::string>();
                // perform read
                output = read_tool(file_path);
            }
            else if (tool_name == "Write") {
                json args = json::parse(tool_call["function"]["arguments"].get<std::string>());
                std::string file_path = args["file_path"].get<std::string>();
                std::string content = args["content"].get<std::string>();
                // perform write
                output = write_tool(file_path, content);
            }
            else if (tool_name == "Bash") {
                json args = json::parse(tool_call["function"]["arguments"].get<std::string>());
                std::string cmd = args["command"].get<std::string>();
                // perform write
                output = command_tool(cmd);
            }

            messages.push_back({
                {"role", "tool"},
                {"tool_call_id", tool_call["id"]},
                {"content", output}
            });

        }
        else {
            std::cout << result["choices"][0]["message"]["content"].get<std::string>();
            return 0;
        }
    }
    
    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cerr << "Logs from your program will appear here!" << std::endl;

    return 0;
}
