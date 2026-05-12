#pragma once

#include "processing_engine_impl.hpp"
#include <fstream>
#include <string>

namespace nervous_system {

class RulesLoader {
public:
    static std::vector<Rule> load_from_json_file(const std::string& filepath) {
        std::vector<Rule> rules;
        
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open rules file: " + filepath);
        }
        
        nlohmann::json config;
        file >> config;
        
        if (!config.contains("rules") || !config["rules"].is_array()) {
            throw std::runtime_error("Invalid rules JSON: 'rules' array not found");
        }
        
        for (const auto& rule_json : config["rules"]) {
            Rule rule;
            
            if (!rule_json.contains("id") || !rule_json.contains("event_type") || 
                !rule_json.contains("alarm")) {
                throw std::runtime_error("Invalid rule: missing required fields");
            }
            
            rule.id = rule_json["id"].get<std::string>();
            rule.event_type = rule_json["event_type"].get<std::string>();
            
            // Serialize condition to JSON string
            if (rule_json.contains("condition") && rule_json["condition"].is_object()) {
                rule.condition_json = rule_json["condition"].dump();
            } else {
                rule.condition_json = "";
            }
            
            // Parse alarm template
            const auto& alarm_json = rule_json["alarm"];
            AlarmDecision alarm_template;
            
            if (alarm_json.contains("alarm_type")) {
                alarm_template.alarm_type = alarm_json["alarm_type"].get<std::string>();
            }
            if (alarm_json.contains("severity")) {
                alarm_template.severity = alarm_json["severity"].get<std::string>();
            }
            if (alarm_json.contains("message")) {
                alarm_template.message = alarm_json["message"].get<std::string>();
            }
            
            rule.alarm_template = alarm_template;
            rules.push_back(rule);
        }
        
        return rules;
    }
    
    static std::vector<Rule> load_from_json_string(const std::string& json_string) {
        std::vector<Rule> rules;
        
        nlohmann::json config = nlohmann::json::parse(json_string);
        
        if (!config.contains("rules") || !config["rules"].is_array()) {
            throw std::runtime_error("Invalid rules JSON: 'rules' array not found");
        }
        
        for (const auto& rule_json : config["rules"]) {
            Rule rule;
            
            if (!rule_json.contains("id") || !rule_json.contains("event_type") || 
                !rule_json.contains("alarm")) {
                throw std::runtime_error("Invalid rule: missing required fields");
            }
            
            rule.id = rule_json["id"].get<std::string>();
            rule.event_type = rule_json["event_type"].get<std::string>();
            
            // Serialize condition to JSON string
            if (rule_json.contains("condition") && rule_json["condition"].is_object()) {
                rule.condition_json = rule_json["condition"].dump();
            } else {
                rule.condition_json = "";
            }
            
            // Parse alarm template
            const auto& alarm_json = rule_json["alarm"];
            AlarmDecision alarm_template;
            
            if (alarm_json.contains("alarm_type")) {
                alarm_template.alarm_type = alarm_json["alarm_type"].get<std::string>();
            }
            if (alarm_json.contains("severity")) {
                alarm_template.severity = alarm_json["severity"].get<std::string>();
            }
            if (alarm_json.contains("message")) {
                alarm_template.message = alarm_json["message"].get<std::string>();
            }
            
            rule.alarm_template = alarm_template;
            rules.push_back(rule);
        }
        
        return rules;
    }
};

}  // namespace nervous_system
