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
            to="/docs">
            Get Started
          </Link>
        </div>
      </div>
    </header>
  );
}

type FeatureItem = {
  title: string;
  description: string;
  link: string;
};

const FeatureList: FeatureItem[] = [
  {
    title: 'Interactive Visualizations',
    description: 'Understand complex OS concepts through animated diagrams and code walkthroughs.',
    link: '/docs/architecture/overview',
  },
  {
    title: 'Theory Meets Code',
    description: 'Every OS concept is mapped to its actual implementation in PintOS.',
    link: '/docs/concepts/threads-and-processes',
  },
  {
    title: 'Project Guides',
    description: 'Step-by-step guides for all four projects: Threads, User Programs, VM, and File Systems.',
    link: '/docs/projects/threads/overview',
  },
  {
    title: 'Roadmap',
    description: 'Track completed features and see what is planned for the future.',
    link: '/docs/roadmap/changelog',
  },
];

function Feature({title, description, link}: FeatureItem) {
  return (
    <div className={clsx('col col--6', styles.feature)}>
      <Link to={link} className={styles.featureLink}>
        <div className="card padding--lg">
          <Heading as="h3">{title}</Heading>
          <p>{description}</p>
        </div>
      </Link>
    </div>
  );
}

export default function Home(): JSX.Element {
  return (
    <Layout
      title="Home"
      description="Learn Operating Systems concepts through the PintOS educational OS">
      <HomepageHeader />
      <main>
        <section className={styles.features}>
          <div className="container">
            <div className="row">
              {FeatureList.map((props, idx) => (
                <Feature key={idx} {...props} />
              ))}
            </div>
          </div>
        </section>
      </main>
    </Layout>
  );
}
