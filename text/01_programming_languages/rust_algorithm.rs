use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq)]
pub struct TokenFrequency {
    pub word: String,
    pub count: usize,
}

pub fn analyze_word_frequencies(content: &str) -> Vec<TokenFrequency> {
    let mut counts: HashMap<String, usize> = HashMap::new();

    for raw_word in content.split_whitespace() {
        let clean: String = raw_word
            .chars()
            .filter(|c| c.is_alphanumeric())
            .map(|c| c.to_lowercase().next().unwrap_or(c))
            .collect();

        if !clean.is_empty() {
            *counts.entry(clean).or_insert(0) += 1;
        }
    }

    let mut result: Vec<TokenFrequency> = counts
        .into_iter()
        .map(|(word, count)| TokenFrequency { word, count })
        .collect();

    result.sort_by(|a, b| b.count.cmp(&a.count).then_with(|| a.word.cmp(&b.word)));
    result
}

fn main() {
    let sample = "Native speed, clean memory, zero overhead, pure performance.";
    let freq = analyze_word_frequencies(sample);
    println!("Word Analysis: {:?}", freq);
}
