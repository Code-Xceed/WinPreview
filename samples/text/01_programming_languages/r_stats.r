# Summary statistics for document reading durations
reading_times <- c(45, 120, 310, 85, 90, 240, 150, 420, 60, 180)

mean_time <- mean(reading_times)
median_time <- median(reading_times)
sd_time <- sd(reading_times)

cat(sprintf("Mean Time: %.2f seconds\n", mean_time))
cat(sprintf("Median Time: %.2f seconds\n", median_time))
cat(sprintf("Standard Deviation: %.2f\n", sd_time))
