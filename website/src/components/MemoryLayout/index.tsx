import React, { useState } from 'react';
import styles from './styles.module.css';

export interface MemoryRegion {
  id: string;
  label: string;
  startAddress: string;
  endAddress: string;
  size?: string;
  color: 'kernel' | 'user' | 'stack' | 'heap' | 'code' | 'data' | 'free';
  description?: string;
  expandable?: boolean;
  subRegions?: MemoryRegion[];
}

interface MemoryLayoutProps {
  regions: MemoryRegion[];
  title?: string;
  showAddresses?: boolean;
  interactive?: boolean;
}

const colorMap = {
  kernel: '#ef4444',
  user: '#3b82f6',
  stack: '#22c55e',
  heap: '#f59e0b',
  code: '#8b5cf6',
  data: '#06b6d4',
  free: '#e5e7eb',
};

export default function MemoryLayout({
  regions,
  title,
  showAddresses = true,
  interactive = true,
}: MemoryLayoutProps): JSX.Element {
  const [expandedRegion, setExpandedRegion] = useState<string | null>(null);
  const [hoveredRegion, setHoveredRegion] = useState<string | null>(null);

  const toggleRegion = (id: string) => {
    if (!interactive) return;
    setExpandedRegion((prev) => (prev === id ? null : id));
  };

  const renderRegion = (region: MemoryRegion, depth = 0) => {
    const isExpanded = expandedRegion === region.id;
    const isHovered = hoveredRegion === region.id;

    return (
      <div key={region.id} className={styles.regionWrapper}>
        <div
          className={`${styles.region} ${isExpanded ? styles.expanded : ''} ${
            isHovered ? styles.hovered : ''
          }`}
          style={{
            backgroundColor: colorMap[region.color],
            marginLeft: depth * 20,
          }}
          onClick={() => region.expandable && toggleRegion(region.id)}
          onMouseEnter={() => setHoveredRegion(region.id)}
          onMouseLeave={() => setHoveredRegion(null)}
        >
          {showAddresses && (
            <div className={styles.addresses}>
              <span className={styles.address}>{region.endAddress}</span>
              <span className={styles.address}>{region.startAddress}</span>
            </div>
          )}

          <div className={styles.content}>
            <span className={styles.label}>{region.label}</span>
            {region.size && (
              <span className={styles.size}>{region.size}</span>
            )}
            {region.expandable && (
              <span className={styles.expandIcon}>
                {isExpanded ? '−' : '+'}
              </span>
            )}
          </div>
        </div>

        {region.description && isHovered && (
          <div className={styles.tooltip}>{region.description}</div>
        )}

        {isExpanded && region.subRegions && (
          <div className={styles.subRegions}>
            {region.subRegions.map((sub) => renderRegion(sub, depth + 1))}
          </div>
        )}
      </div>
    );
  };

  return (
    <div className={styles.container}>
      {title && <h4 className={styles.title}>{title}</h4>}
      <div className={styles.legend}>
        {Object.entries(colorMap).map(([key, color]) => (
          <div key={key} className={styles.legendItem}>
            <span
              className={styles.legendColor}
              style={{ backgroundColor: color }}
            />
            <span className={styles.legendLabel}>{key}</span>
          </div>
        ))}
      </div>
      <div className={styles.layout}>
        {regions.map((region) => renderRegion(region))}
      </div>
      <div className={styles.hint}>
        {interactive && 'Click on regions with + to expand'}
      </div>
    </div>
  );
}
