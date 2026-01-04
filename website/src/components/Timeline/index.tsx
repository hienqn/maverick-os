import React from 'react';
import Link from '@docusaurus/Link';
import styles from './styles.module.css';

export interface TimelineItem {
  version: string;
  date: string;
  title: string;
  items: string[];
  link?: string;
  highlight?: boolean;
}

interface TimelineProps {
  items: TimelineItem[];
  title?: string;
}

export default function Timeline({ items, title }: TimelineProps): JSX.Element {
  return (
    <div className={styles.container}>
      {title && <h3 className={styles.title}>{title}</h3>}
      <div className={styles.timeline}>
        {items.map((item, idx) => (
          <div
            key={item.version}
            className={`${styles.item} ${item.highlight ? styles.highlight : ''}`}
          >
            <div className={styles.dot} />
            <div className={styles.content}>
              <div className={styles.header}>
                <span className={styles.version}>{item.version}</span>
                <span className={styles.date}>{item.date}</span>
              </div>
              <h4 className={styles.itemTitle}>{item.title}</h4>
              <ul className={styles.featureList}>
                {item.items.map((feature, fidx) => (
                  <li key={fidx}>{feature}</li>
                ))}
              </ul>
              {item.link && (
                <Link to={item.link} className={styles.link}>
                  Learn more →
                </Link>
              )}
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
