import React, { useState, useEffect } from 'react';

interface MetricCardProps {
  title: string;
  metric: number;
  suffix?: string;
  trendPercentage: number;
}

export const MetricCard: React.FC<MetricCardProps> = ({
  title,
  metric,
  suffix = '',
  trendPercentage,
}) => {
  const [animatedValue, setAnimatedValue] = useState(0);

  useEffect(() => {
    let start = 0;
    const duration = 800;
    const step = metric / (duration / 16);

    const timer = setInterval(() => {
      start += step;
      if (start >= metric) {
        setAnimatedValue(metric);
        clearInterval(timer);
      } else {
        setAnimatedValue(Math.floor(start));
      }
    }, 16);

    return () => clearInterval(timer);
  }, [metric]);

  const isPositive = trendPercentage >= 0;

  return (
    <div className="p-4 rounded-xl bg-zinc-900 border border-zinc-800 shadow-lg">
      <p className="text-xs font-medium text-zinc-400 uppercase tracking-wider">{title}</p>
      <div className="flex items-baseline gap-2 mt-2">
        <span className="text-2xl font-bold text-white">
          {animatedValue.toLocaleString()}{suffix}
        </span>
        <span className={`text-xs font-semibold ${isPositive ? 'text-emerald-400' : 'text-rose-400'}`}>
          {isPositive ? '↑' : '↓'} {Math.abs(trendPercentage)}%
        </span>
      </div>
    </div>
  );
};
