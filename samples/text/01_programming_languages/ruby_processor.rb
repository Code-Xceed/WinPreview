# frozen_string_literal: true

module WinPreview
  class LogParser
    attr_reader :entries

    def initialize
      @entries = []
    end

    def parse_line(line)
      return if line.strip.empty?

      if line =~ /\A\[(?<level>\w+)\]\s+(?<timestamp>[\d\-:\s]+)\s+-\s+(?<msg>.*)\z/
        @entries << {
          level: Regexp.last_match[:level].upcase,
          timestamp: Regexp.last_match[:timestamp],
          message: Regexp.last_match[:msg]
        }
      end
    end
  end
end
