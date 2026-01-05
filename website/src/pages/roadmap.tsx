import React from 'react';
import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';
import styles from './roadmap.module.css';

interface RoadmapItem {
  title: string;
  status: 'done' | 'in-progress' | 'planned';
  version?: string;
  description: string;
}

const roadmapData: RoadmapItem[] = [
  {
    title: 'Copy-on-Write Fork',
    status: 'done',
    version: 'v1.4.0',
    description: 'Efficient process forking with lazy page copying',
  },
  {
    title: 'Virtual Memory',
    status: 'done',
    version: 'v1.3.0',
    description: 'Demand paging, frame table, swap space, mmap',
  },
  {
    title: 'Write-Ahead Logging',
    status: 'done',
    version: 'v1.2.0',
    description: 'Crash-consistent filesystem transactions',
  },
  {
    title: 'Symbolic Links',
    status: 'done',
    version: 'v1.1.0',
    description: 'symlink() and readlink() support',
  },
  {
    title: 'Buffer Cache & Extensible Files',
    status: 'done',
    version: 'v1.0.0',
    description: '64-block cache, indexed inodes up to 8MB',
  },
  {
    title: 'SMP Support',
    status: 'planned',
    description: 'Symmetric multiprocessing with spinlocks, per-CPU data, APIC',
  },
  {
    title: 'VFS Layer',
    status: 'planned',
    description: 'Virtual filesystem abstraction for multiple FS types',
  },
  {
    title: 'RISC-V Port',
    status: 'planned',
    description: 'Port to RISC-V RV64GC architecture',
  },
];

function StatusBadge({ status }: { status: RoadmapItem['status'] }) {
  const labels = {
    done: 'Complete',
    'in-progress': 'In Progress',
    planned: 'Planned',
  };

  return <span className={`${styles.badge} ${styles[status]}`}>{labels[status]}</span>;
}

export default function Roadmap(): JSX.Element {
  const completed = roadmapData.filter((item) => item.status === 'done');
  const planned = roadmapData.filter((item) => item.status === 'planned');

  return (
    <Layout title="Roadmap" description="PintOS development roadmap">
      <main className={styles.main}>
        <div className="container">
          <h1 className={styles.title}>Roadmap</h1>
          <p className={styles.subtitle}>
            Track the development progress of PintOS features
          </p>

          <section className={styles.section}>
            <h2>Completed Features</h2>
            <div className={styles.grid}>
              {completed.map((item, idx) => (
                <div key={idx} className={`${styles.card} ${styles.done}`}>
                  <div className={styles.cardHeader}>
                    <StatusBadge status={item.status} />
                    {item.version && (
                      <span className={styles.version}>{item.version}</span>
                    )}
                  </div>
                  <h3>{item.title}</h3>
                  <p>{item.description}</p>
                </div>
              ))}
            </div>
          </section>

          <section className={styles.section}>
            <h2>Planned Features</h2>
            <div className={styles.grid}>
              {planned.map((item, idx) => (
                <div key={idx} className={`${styles.card} ${styles.planned}`}>
                  <div className={styles.cardHeader}>
                    <StatusBadge status={item.status} />
                  </div>
                  <h3>{item.title}</h3>
                  <p>{item.description}</p>
                </div>
              ))}
            </div>
          </section>

          <section className={styles.section}>
            <h2>Version History</h2>
            <p>
              For a detailed changelog, see the{' '}
              <Link to="https://github.com/hienqn/maverick-os/releases">
                GitHub Releases
              </Link>.
            </p>
          </section>
        </div>
      </main>
    </Layout>
  );
}
