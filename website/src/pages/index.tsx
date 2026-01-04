import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import Heading from '@theme/Heading';

import styles from './index.module.css';

function HomepageHeader() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <header className={clsx('hero hero--primary', styles.heroBanner)}>
      <div className="container">
        <Heading as="h1" className="hero__title">
          {siteConfig.title}
        </Heading>
        <p className="hero__subtitle">{siteConfig.tagline}</p>
        <div className={styles.buttons}>
          <Link
            className="button button--secondary button--lg"
            to="/docs/intro">
            Get Started
          </Link>
          <Link
            className="button button--outline button--lg"
            style={{marginLeft: '1rem', color: 'white', borderColor: 'white'}}
            to="/docs/concepts/threads-and-processes">
            Explore Concepts
          </Link>
        </div>
      </div>
    </header>
  );
}

type FeatureItem = {
  title: string;
  icon: string;
  description: string;
  link: string;
};

const FeatureList: FeatureItem[] = [
  {
    title: 'Interactive Visualizations',
    icon: '🎯',
    description: 'Understand complex OS concepts through animated diagrams, memory layouts, and code walkthroughs that bring the kernel to life.',
    link: '/docs/architecture/overview',
  },
  {
    title: 'Theory Meets Code',
    icon: '📚',
    description: 'Every OS concept is mapped to its actual implementation in PintOS. See how textbook theory becomes working code.',
    link: '/docs/concepts/threads-and-processes',
  },
  {
    title: 'Project Guides',
    icon: '🛠️',
    description: 'Step-by-step guides for all four projects: Threads, User Programs, Virtual Memory, and File Systems.',
    link: '/docs/projects/threads/overview',
  },
  {
    title: 'Deep Dives',
    icon: '🔬',
    description: 'Line-by-line analysis of critical code paths like context switching, page fault handling, and crash recovery.',
    link: '/docs/deep-dives/context-switch-assembly',
  },
];

function Feature({title, icon, description, link}: FeatureItem) {
  return (
    <div className={clsx('col col--6', styles.feature)}>
      <Link to={link} className={styles.featureLink}>
        <div className="feature-card">
          <div className={styles.featureIcon}>{icon}</div>
          <Heading as="h3">{title}</Heading>
          <p>{description}</p>
        </div>
      </Link>
    </div>
  );
}

function HomepageFeatures() {
  return (
    <section className={styles.features}>
      <div className="container">
        <div className="row">
          {FeatureList.map((props, idx) => (
            <Feature key={idx} {...props} />
          ))}
        </div>
      </div>
    </section>
  );
}

function ArchitecturePreview() {
  return (
    <section className={styles.architectureSection}>
      <div className="container">
        <Heading as="h2" className={styles.sectionTitle}>
          PintOS Architecture
        </Heading>
        <p className={styles.sectionSubtitle}>
          A complete educational operating system with modern features
        </p>
        <div className={styles.architectureDiagram}>
          <pre className={styles.asciiDiagram}>
{`┌─────────────────────────────────────────────────────────────────┐
│                        User Programs                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐ │
│  │   echo   │  │    ls    │  │   cat    │  │  user threads    │ │
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│                     System Call Interface                       │
│            int 0x30 → syscall_handler() → dispatch              │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                     KERNEL SPACE                            ││
│  ├──────────────┬──────────────┬───────────────────────────────┤│
│  │   Threads    │   Userprog   │         Virtual Memory        ││
│  │  ┌────────┐  │  ┌────────┐  │  ┌──────────┐ ┌────────────┐  ││
│  │  │Priority│  │  │Process │  │  │   SPT    │ │   Frame    │  ││
│  │  │Donation│  │  │ Mgmt   │  │  │(per proc)│ │   Table    │  ││
│  │  ├────────┤  │  ├────────┤  │  ├──────────┤ ├────────────┤  ││
│  │  │ MLFQS  │  │  │  Fork  │  │  │  Demand  │ │   Clock    │  ││
│  │  │Scheduler│  │  │ (COW) │  │  │  Paging  │ │  Eviction  │  ││
│  │  └────────┘  │  └────────┘  │  └──────────┘ └────────────┘  ││
│  ├──────────────┴──────────────┴───────────────────────────────┤│
│  │                      File System                            ││
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────────┐  ││
│  │  │  Buffer  │  │ Indexed  │  │ Symbolic │  │   Write-    │  ││
│  │  │  Cache   │  │  Inodes  │  │  Links   │  │ Ahead Log   │  ││
│  │  └──────────┘  └──────────┘  └──────────┘  └─────────────┘  ││
│  └─────────────────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────────────────┤
│                         Devices                                 │
│        Timer │ IDE Disk │ Keyboard │ VGA │ Serial               │
└─────────────────────────────────────────────────────────────────┘`}
          </pre>
        </div>
        <div className={styles.versionBadges}>
          <span className={styles.badge}>v1.4.0</span>
          <span className={styles.badge}>4 Projects</span>
          <span className={styles.badge}>37+ Syscalls</span>
          <span className={styles.badge}>COW Fork</span>
          <span className={styles.badge}>WAL</span>
        </div>
      </div>
    </section>
  );
}

function QuickLinks() {
  const links = [
    {title: 'Project 1: Threads', path: '/docs/projects/threads/overview', desc: 'Priority scheduling, MLFQS'},
    {title: 'Project 2: User Programs', path: '/docs/projects/userprog/overview', desc: 'System calls, process management'},
    {title: 'Project 3: Virtual Memory', path: '/docs/projects/vm/overview', desc: 'Demand paging, swap, COW fork'},
    {title: 'Project 4: File System', path: '/docs/projects/filesys/overview', desc: 'Buffer cache, WAL, extensible files'},
  ];

  return (
    <section className={styles.quickLinks}>
      <div className="container">
        <Heading as="h2" className={styles.sectionTitle}>
          Project Guides
        </Heading>
        <div className={styles.projectGrid}>
          {links.map((link, idx) => (
            <Link key={idx} to={link.path} className={styles.projectCard}>
              <span className={styles.projectNumber}>{idx + 1}</span>
              <div>
                <strong>{link.title}</strong>
                <p>{link.desc}</p>
              </div>
            </Link>
          ))}
        </div>
      </div>
    </section>
  );
}

export default function Home(): JSX.Element {
  const {siteConfig} = useDocusaurusContext();
  return (
    <Layout
      title="Home"
      description="Learn Operating Systems concepts through the PintOS educational OS implementation">
      <HomepageHeader />
      <main>
        <HomepageFeatures />
        <ArchitecturePreview />
        <QuickLinks />
      </main>
    </Layout>
  );
}
