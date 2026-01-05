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
            to="/blog">
            Browse All Posts
          </Link>
        </div>
      </div>
    </header>
  );
}

type FeaturedPostItem = {
  title: string;
  description: string;
  link: string;
  tags: string[];
};

const FeaturedPosts: FeaturedPostItem[] = [
  {
    title: 'Understanding Virtual Memory',
    description: 'Learn how virtual memory works with interactive memory layout visualizations and quizzes.',
    link: '/blog/understanding-virtual-memory',
    tags: ['OS Concepts', 'Virtual Memory'],
  },
  {
    title: 'Context Switching Explained',
    description: 'Visualize how the CPU switches between processes with animated diagrams and code walkthroughs.',
    link: '/blog/context-switching-explained',
    tags: ['OS Concepts', 'Scheduling'],
  },
  {
    title: 'Hash Tables in PintOS',
    description: 'Deep dive into the hash table implementation used throughout PintOS with interactive diagrams.',
    link: '/blog/hash-tables-in-pintos',
    tags: ['Data Structures', 'PintOS'],
  },
];

function FeaturedPost({title, description, link, tags}: FeaturedPostItem) {
  return (
    <div className={clsx('col col--4', styles.featuredPost)}>
      <Link to={link} className={styles.postLink}>
        <div className="card padding--lg">
          <div className={styles.postTags}>
            {tags.map((tag, idx) => (
              <span key={idx} className={styles.postTag}>{tag}</span>
            ))}
          </div>
          <Heading as="h3">{title}</Heading>
          <p>{description}</p>
          <span className={styles.readMore}>Read more &rarr;</span>
        </div>
      </Link>
    </div>
  );
}

const topics = [
  { name: 'OS Concepts', link: '/blog/tags/os-concepts' },
  { name: 'Data Structures', link: '/blog/tags/data-structures' },
  { name: 'Algorithms', link: '/blog/tags/algorithms' },
  { name: 'Systems Programming', link: '/blog/tags/systems' },
  { name: 'PintOS', link: '/blog/tags/pintos' },
];

function TopicsSection() {
  return (
    <section className={styles.topicsSection}>
      <div className="container">
        <Heading as="h2" className={styles.sectionTitle}>
          Browse by Topic
        </Heading>
        <div className={styles.topicsGrid}>
          {topics.map((topic, idx) => (
            <Link key={idx} to={topic.link} className={styles.topicCard}>
              <span className={styles.topicName}>{topic.name}</span>
            </Link>
          ))}
        </div>
      </div>
    </section>
  );
}

export default function Home(): JSX.Element {
  return (
    <Layout
      title="Home"
      description="Learn Operating Systems concepts through interactive blog posts">
      <HomepageHeader />
      <main>
        <section className={styles.featuredSection}>
          <div className="container">
            <Heading as="h2" className={styles.sectionTitle}>
              Featured Posts
            </Heading>
            <div className="row">
              {FeaturedPosts.map((props, idx) => (
                <FeaturedPost key={idx} {...props} />
              ))}
            </div>
          </div>
        </section>
        <TopicsSection />
      </main>
    </Layout>
  );
}
