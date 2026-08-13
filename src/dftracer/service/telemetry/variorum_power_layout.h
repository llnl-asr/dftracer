#ifndef DFTRACER_VARIORUM_POWER_LAYOUT_H
#define DFTRACER_VARIORUM_POWER_LAYOUT_H

// Shape of a variorum power payload: how its JSON flattens out, and which
// family of measurement each reading belongs to.
//
// Kept apart from the collector, and free of every DFTracer header, so the
// parsing and the family rules can be unit tested on their own -- on a build
// with no variorum linked in, and without standing up a buffer manager.

#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace dftracer {

/**
 * @brief One reading, flattened out of variorum's nested JSON.
 *
 * The name is the JSON path with the hostname wrapper removed and the levels
 * joined by '.', e.g. "power_node_watts" or "socket_0.power_gpu_watts.GPU_0".
 */
struct VariorumPowerSample {
  std::string metric_name;
  double metric_value;
};

/**
 * @brief One family of measurement, with every reading that belongs to it.
 *
 * A family is the thing being measured -- cpu, gpu, memory, network, node --
 * taken from variorum's own "power_<family>_watts" key. Each reading keeps only
 * what identifies it within the family, so a gpu family holds "socket_0.GPU_0"
 * rather than repeating "power_gpu_watts" in every key, and a reading with
 * nothing left to distinguish it is named "total".
 */
struct VariorumPowerFamily {
  std::string family;
  std::vector<std::pair<std::string, double>> readings;
};

/**
 * @brief Where one flattened key belongs: its family, and its name within it.
 */
struct VariorumKeyPlacement {
  std::string family;
  std::string reading;
};

namespace variorum_layout {

// Readings that name no device. Kept rather than dropped, but held apart so
// nothing here is read as watts.
inline const char* other_family() { return "other"; }

// What a reading is called inside its family when the family name was the whole
// path, as for power_node_watts.
inline const char* total_reading() { return "total"; }

// The wall clock variorum stamps its own reading with. The counter record
// already carries the trace's timestamp, in the trace's time base, so keeping
// variorum's would only add a second and disagreeing clock.
inline const char* timestamp_key() { return "timestamp"; }

// variorum's payload is a handful of readings a few levels deep. Anything
// nested further than this is not power data, so refuse it rather than
// recursing on whatever was handed to us.
inline int max_depth() { return 32; }

/**
 * @brief Canonical spelling of a family variorum names in its own way.
 *
 * variorum abbreviates some and pluralises others depending on the key, so
 * "mem", "memory", "gpu" and "gpus" all have to land in one family for a
 * consumer to be able to select it. Empty when the word names no device.
 */
inline std::string canonical_family(const std::string& word) {
  if (word == "cpu" || word == "cpus") return "cpu";
  if (word == "gpu" || word == "gpus") return "gpu";
  if (word == "mem" || word == "memory") return "memory";
  if (word == "net" || word == "network") return "network";
  if (word == "node") return "node";
  return std::string();
}

/**
 * @brief Pull the family out of a "power_<family>_watts" segment.
 *
 * Empty when the segment does not name one. A family variorum adds later is
 * taken at face value, so this does not have to be kept in step with its
 * release notes.
 */
inline std::string power_family_of_segment(const std::string& segment) {
  static const std::string prefix = "power_";
  static const std::string suffix = "_watts";
  if (segment.size() <= prefix.size() + suffix.size()) return std::string();
  if (segment.compare(0, prefix.size(), prefix) != 0) return std::string();
  if (segment.compare(segment.size() - suffix.size(), suffix.size(), suffix) !=
      0) {
    return std::string();
  }
  const std::string family = segment.substr(
      prefix.size(), segment.size() - prefix.size() - suffix.size());
  const std::string canonical = canonical_family(family);
  return canonical.empty() ? family : canonical;
}

/**
 * @brief Family named anywhere in a path, for keys that are not watts.
 *
 * variorum reports a few things about a device alongside its power --
 * num_gpus_per_socket is the one every GPU node has -- and those belong with
 * the device they describe rather than off on their own. The path is split on
 * both separators variorum uses so "num_gpus_per_socket" is seen as the words
 * it is made of.
 */
inline std::string device_family_of_path(const std::string& path) {
  std::string word;
  for (size_t position = 0; position <= path.size(); position++) {
    const char ch = position < path.size() ? path[position] : '_';
    if (ch == '.' || ch == '_') {
      const std::string family = canonical_family(word);
      if (!family.empty()) return family;
      word.clear();
      continue;
    }
    word.push_back(ch);
  }
  return std::string();
}

/**
 * @brief Decide the family and in-family name for one flattened key.
 *
 * This is the whole of the string work a reading costs, which is why the
 * collector works it out once for every key at startup and does nothing but a
 * lookup per sample after that.
 */
inline VariorumKeyPlacement place_key(const std::string& key) {
  std::string family;
  std::string reading;
  size_t start = 0;
  while (start <= key.size()) {
    size_t dot = key.find('.', start);
    if (dot == std::string::npos) dot = key.size();
    const std::string segment = key.substr(start, dot - start);
    start = dot + 1;

    if (family.empty()) {
      const std::string candidate = power_family_of_segment(segment);
      if (!candidate.empty()) {
        family = candidate;
        continue;  // the family segment is not part of the reading's name
      }
    }
    if (!reading.empty()) reading += ".";
    reading += segment;
  }

  // Not a watts reading, but it may still be about a device: keep it with that
  // device rather than in a bucket of its own. The whole path stays as the name
  // here, since no segment was consumed to find the family.
  if (family.empty()) family = device_family_of_path(key);
  if (family.empty()) family = other_family();
  if (reading.empty()) reading = total_reading();
  return {family, reading};
}

/**
 * @brief Walks a JSON document and emits every numeric leaf with its path.
 *
 * Written out here rather than pulled in from jansson (which variorum uses
 * internally) so the flattening can be unit tested on a build with no variorum,
 * and so the service does not grow a JSON dependency for one collector. Only
 * what a reading needs is kept: objects, arrays, and numbers. Strings, booleans
 * and nulls are parsed so the walk stays in step, then discarded, since a
 * counter value has to be a number.
 */
class JsonFlattener {
 public:
  JsonFlattener(const std::string& text, std::vector<VariorumPowerSample>& out)
      : text(text), pos(0), out(out) {}

  /** @brief Parse the whole document. False if it is not valid JSON. */
  bool run() {
    if (!parse_value(std::string(), 0)) return false;
    skip_ws();
    return pos == text.size();
  }

 private:
  const std::string& text;
  size_t pos;
  std::vector<VariorumPowerSample>& out;

  void skip_ws() {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
                                 text[pos] == '\n' || text[pos] == '\r')) {
      pos++;
    }
  }

  bool at(char ch) {
    skip_ws();
    return pos < text.size() && text[pos] == ch;
  }

  bool consume(char ch) {
    if (!at(ch)) return false;
    pos++;
    return true;
  }

  static std::string join(const std::string& path, const std::string& leaf) {
    if (path.empty()) return leaf;
    return path + "." + leaf;
  }

  void emit(const std::string& path, double value) {
    if (path.empty()) return;
    // Skip variorum's own timestamp, wherever in the tree it sits.
    const size_t last = path.rfind('.');
    const std::string leaf =
        last == std::string::npos ? path : path.substr(last + 1);
    if (leaf == timestamp_key()) return;
    // A non-finite reading is a failed measurement, not a value worth tracing.
    if (!std::isfinite(value)) return;
    out.push_back({path, value});
  }

  bool parse_value(const std::string& path, int depth) {
    if (depth > max_depth()) return false;
    skip_ws();
    if (pos >= text.size()) return false;

    const char ch = text[pos];
    if (ch == '{') return parse_object(path, depth);
    if (ch == '[') return parse_array(path, depth);
    if (ch == '"') {
      std::string ignored;
      return parse_string(ignored);
    }
    if (ch == 't') return parse_literal("true");
    if (ch == 'f') return parse_literal("false");
    if (ch == 'n') return parse_literal("null");

    double value = 0.0;
    if (!parse_number(value)) return false;
    emit(path, value);
    return true;
  }

  bool parse_object(const std::string& path, int depth) {
    if (!consume('{')) return false;
    if (consume('}')) return true;
    while (true) {
      skip_ws();
      std::string key;
      if (!parse_string(key)) return false;
      if (!consume(':')) return false;
      if (!parse_value(join(path, key), depth + 1)) return false;
      if (consume(',')) continue;
      return consume('}');
    }
  }

  bool parse_array(const std::string& path, int depth) {
    if (!consume('[')) return false;
    if (consume(']')) return true;
    // Power payloads are objects all the way down, but an array is valid JSON
    // and the index keeps each element's name distinct.
    for (size_t element = 0;; element++) {
      if (!parse_value(join(path, std::to_string(element)), depth + 1)) {
        return false;
      }
      if (consume(',')) continue;
      return consume(']');
    }
  }

  bool parse_string(std::string& value) {
    if (!consume('"')) return false;
    value.clear();
    while (pos < text.size()) {
      const char ch = text[pos++];
      if (ch == '"') return true;
      if (ch != '\\') {
        value.push_back(ch);
        continue;
      }
      if (pos >= text.size()) return false;
      const char escaped = text[pos++];
      switch (escaped) {
        case 'n':
          value.push_back('\n');
          break;
        case 't':
          value.push_back('\t');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 'b':
          value.push_back('\b');
          break;
        case 'f':
          value.push_back('\f');
          break;
        // \" \\ \/ stand for themselves, and \uXXXX is kept verbatim: variorum
        // keys are plain ASCII, and copying the escape through keeps the scan
        // in step without a UTF-8 decoder.
        default:
          value.push_back(escaped);
          break;
      }
    }
    return false;  // unterminated
  }

  bool parse_literal(const char* literal) {
    const std::string expected(literal);
    if (text.compare(pos, expected.size(), expected) != 0) return false;
    pos += expected.size();
    return true;
  }

  bool parse_number(double& value) {
    // JSON numbers start with a digit or a minus sign. Checking first keeps
    // strtod from accepting the "inf"/"nan"/"0x" forms it also understands.
    if (text[pos] != '-' && (text[pos] < '0' || text[pos] > '9')) return false;
    const char* start = text.c_str() + pos;
    char* end = nullptr;
    value = std::strtod(start, &end);
    if (end == start) return false;
    pos += static_cast<size_t>(end - start);
    return true;
  }
};

/**
 * @brief Drop the hostname object every variorum payload is wrapped in.
 *
 * Done on the flattened names rather than structurally so it only applies when
 * the whole document really does sit under one key: if a payload ever grows a
 * second top-level entry, the names keep their full path instead of silently
 * losing a level.
 */
inline void strip_common_root(std::vector<VariorumPowerSample>& samples) {
  if (samples.empty()) return;

  const size_t separator = samples.front().metric_name.find('.');
  if (separator == std::string::npos) return;
  const std::string root = samples.front().metric_name.substr(0, separator + 1);

  for (const auto& sample : samples) {
    if (sample.metric_name.compare(0, root.size(), root) != 0) return;
  }
  for (auto& sample : samples) {
    sample.metric_name = sample.metric_name.substr(root.size());
  }
}

/**
 * @brief Flatten a variorum power JSON payload into samples.
 *
 * Returns an empty vector for anything that is not parseable JSON: half a
 * reading looks like a whole one once it is in a trace.
 */
inline std::vector<VariorumPowerSample> flatten_power_json(
    const std::string& payload) {
  std::vector<VariorumPowerSample> samples;
  if (payload.empty()) return samples;

  JsonFlattener flattener(payload, samples);
  if (!flattener.run()) {
    samples.clear();
    return samples;
  }
  strip_common_root(samples);
  return samples;
}

}  // namespace variorum_layout
}  // namespace dftracer

#endif  // DFTRACER_VARIORUM_POWER_LAYOUT_H
